/*
 * Copyright (C) 2015 by Glenn Hickey (hickey@soe.ucsc.edu)
 *
 * Released under the MIT license, see LICENSE.txt
 */

#include <sstream>

#include "sgbuilder.h"
#include "halBlockMapper.h"

using namespace std;
using namespace hal;

SGBuilder::SGBuilder() : _sg(0), _root(0), _lookup(0), _mapMrca(0),
                         _referenceDupes(true), _noSubMode(false)
{

}

SGBuilder::~SGBuilder()
{
  clear();
}

void SGBuilder::init(AlignmentConstPtr alignment, const Genome* root,
                     bool referenceDupes, bool noSubMode)
{
  clear();
  _alignment = alignment;
  _sg = new SideGraph();
  _root = root;
  _referenceDupes = referenceDupes;
  _noSubMode = noSubMode;
}

void SGBuilder::clear()
{
  delete _sg;
  _sg = NULL;
  _alignment = AlignmentConstPtr();
  for (GenomeLUMap::iterator i = _luMap.begin(); i != _luMap.end(); ++i)
  {
    delete i->second;
  }
  _luMap.clear();
  _lookup = NULL;
  _seqMapBack.clear();
  _mapPath.clear();
  _mapMrca = NULL;
  _firstGenomeName.erase();
  _halSequences.clear();
}

const SideGraph* SGBuilder::getSideGraph() const
{
  return _sg;
}

size_t SGBuilder::getSequenceString(const SGSequence* sgSequence,
                                    string& outString,
                                    sg_int_t pos,
                                    sg_int_t length) const
{
  SequenceMapBack::const_iterator i = _seqMapBack.find(sgSequence);
  assert(i != _seqMapBack.end());
  const Sequence* halSeq = i->second.first;
  hal_index_t halPos = i->second.second;
  hal_index_t len = length == -1 ? sgSequence->getLength() : length;
  if (_noSubMode == true && halSeq->getGenome()->getParent() == NULL)
  {
    // adams root has not sequence. we infer it from children as a hack.
    getRootSubString(outString, halSeq, halPos + pos, len);
  }
  else
  {
    halSeq->getSubString(outString, halPos + pos, len);
  }
  return outString.length();
}

const string& SGBuilder::getPrimaryGenomeName() const
{
  return _firstGenomeName;
}

string SGBuilder::getHalGenomeName(const SGSequence* sgSequence) const
{
  SequenceMapBack::const_iterator i = _seqMapBack.find(sgSequence);
  assert(i != _seqMapBack.end());
  const Sequence* halSeq = i->second.first;
  return halSeq->getGenome()->getName();
}

const vector<const Sequence*>& SGBuilder::getHalSequences() const
{
  return _halSequences;
}

void SGBuilder::getHalSequencePath(const Sequence* halSeq,
                                   vector<SGSegment>& outPath) const
{
  GenomeLUMap::const_iterator lui = _luMap.find(halSeq->getGenome()->getName());
  assert(lui != _luMap.end());
  SGPosition start(halSeq->getArrayIndex(), 0);
  SGPosition end(halSeq->getArrayIndex(),
                 max((hal_size_t)0, halSeq->getSequenceLength() - 1));
  lui->second->getPath(start, end, outPath);
}

void SGBuilder::addGenome(const Genome* genome,
                          const Sequence* sequence, 
                          hal_index_t start,
                          hal_index_t length)
{
  cout << "\n-----------\n" << "ADDING " << genome->getName() << endl;
  assert(_luMap.find(genome->getName()) == _luMap.end());
  if (_firstGenomeName.empty())
  {
    // make note of reference genome to tell if sequences are
    // derived or not in sql. 
    _firstGenomeName = genome->getName();
  }

  start = std::max((hal_index_t)0, start);
  if (length <= 0)
  {
    if (sequence == NULL)
    {
      length = genome->getSequenceLength() - start;
    }
    else
    {
      length = sequence->getSequenceLength() - start;
    }
  }
  
  // Add genomes sequences to the lookup structure
  vector<string> seqNames;
  if (sequence != NULL)
  {
    seqNames.push_back(sequence->getName());
  }
  else
  {
    SequenceIteratorConstPtr si = genome->getSequenceIterator();
    SequenceIteratorConstPtr sie = genome->getSequenceEndIterator();
    for (; si != sie; si->toNext())
    {
      const Sequence* curSequence = si->getSequence();
      if (start <= curSequence->getEndPosition() &&
          (start + length-1) >= curSequence->getStartPosition())
      {
        seqNames.push_back(curSequence->getName());
      }
    }
  }
  _lookup = new SGLookup();
  _lookup->init(seqNames);
  _luMap.insert(pair<string, SGLookup*>(genome->getName(), _lookup));

  // If sequence is not NULL, start in sequence coordinates and
  // need to be converted
  if (sequence != NULL)
  {
    start += sequence->getStartPosition();
  }

  // Compute the target
  const Genome* target = getTarget(genome);
  // Update the mapping structures.  Should verify with Joel what
  // these new parameters mean. 
  if (target != NULL)
  {
    set<const Genome*> inputSet;
    inputSet.insert(genome);
    inputSet.insert(target);
    _mapMrca = getLowestCommonAncestor(inputSet);
    inputSet.clear();
    inputSet.insert(_root);
    inputSet.insert(target);
    getGenomesInSpanningTree(inputSet, _mapPath);
  }

  // Convert sequence by sequence
  for (size_t i = 0; i < seqNames.size(); ++i)
  {
    const Sequence* curSequence = genome->getSequence(seqNames[i]);
    hal_index_t endPos = start + length - 1;
    hal_index_t curStart = std::max(start, curSequence->getStartPosition());
    hal_index_t curEnd = std::min(endPos, curSequence->getEndPosition());

    // note all coordinates global
    if (curStart <= curEnd)
    {
      mapSequence(curSequence, curStart, curEnd, target);
      _halSequences.push_back(curSequence);
    }
  }
}

const Genome* SGBuilder::getTarget(const Genome* genome)
{
  // This code may be too dumb to be any more than a placeholder, but
  // have no short term designs on huge trees...

  // find the nearest genome in our hal tree that's already been added
  // to the lookup structure.
  size_t dist = numeric_limits<size_t>::max();
  const Genome* best = NULL;
  for (GenomeLUMap::const_iterator i = _luMap.begin(); i != _luMap.end(); ++i)
  {
    const Genome* candidate = _alignment->openGenome(i->first);

    set<const Genome*> inputSet;
    inputSet.insert(genome);
    inputSet.insert(candidate);
    set<const Genome*> spanningTree;
    getGenomesInSpanningTree(inputSet, spanningTree);
    if (spanningTree.size() > 1 && spanningTree.size() < dist)
    {
      dist = spanningTree.size();
      best = candidate;
    }
  }
  return best;
}

SGSequence* SGBuilder::createSGSequence(const Sequence* sequence,
                                        hal_index_t startOffset,
                                        hal_index_t length)
{
  assert(sequence != NULL);
  assert(startOffset >= 0);
  assert(length > 0);
 
  // make a new Side Graph Sequence
  stringstream name;
  name << sequence->getFullName();
  if (length < (hal_index_t)sequence->getSequenceLength())
  {
    name << "_" << startOffset << "_" << length;
  }  
  SGSequence* sgSeq = new SGSequence(-1, length, name.str());

  // add to the Side Graph
  _sg->addSequence(sgSeq);
  // keep record of where it came from (ie to trace back from the side graph
  // to hal
  _seqMapBack.insert(pair<SGSequence*, pair<const Sequence*, hal_index_t> >(
                       sgSeq,
                       pair<const Sequence*, hal_index_t>(sequence,
                                                          startOffset)));
  
  // keep record in other direction (ie to map from hal into the side graph)
  sg_seqid_t halSequenceID = (sg_seqid_t)sequence->getArrayIndex();
  assert(halSequenceID >= 0);
  cout << "Create Sequence " << *sgSeq << " from hal(" << halSequenceID
       << ", " << startOffset << " l=" << length << endl;

  _lookup->addInterval(SGPosition(halSequenceID, startOffset),
                      SGPosition(sgSeq->getID(), 0),
                      length, false);

  return sgSeq;
}

const SGJoin* SGBuilder::createSGJoin(const SGSide& side1, const SGSide& side2)
{
  SGJoin* join = new SGJoin(side1, side2);
  return _sg->addJoin(join);

  // if two consecutive joins are end to end, we merge into a single join
  if (_lastJoin != NULL && _lastJoin->getSide2() == join->getSide1())
  {
    join->setSide1(_lastJoin->getSide1());
    assert(join->getSide2() != join->getSide1());
    delete _lastJoin;
    _lastJoin = NULL;
  }
  
  // write the last join
  if (_lastJoin != NULL)
  {
    //const SGJoin* j = _sg->addJoin(_lastJoin);
    cout << "add join " << *_lastJoin << endl;
    // add two steps to our path.
    // note: once last join's in the side graph, we've lost all
    // control of it.  
    _lastJoin = NULL;
  }

  // filter trivial join
  if (abs(join->getSide1().getBase().getPos() -
          join->getSide2().getBase().getPos()) == 1 &&
      join->getSide1().getForward() !=
      join->getSide2().getForward())
  {
    cout << "filter trivial " << *join << endl;
    delete join;
    join = NULL;
  }
  else
  {
    _lastJoin = join;
  }
  
  return join;
}

void SGBuilder::mapSequence(const Sequence* sequence,
                            hal_index_t globalStart,
                            hal_index_t globalEnd,
                            const Genome* target)
{
  const Genome* genome = sequence->getGenome();
  cout << "Map Sequence " << sequence->getFullName() << endl;

  // first genome added: it's the reference so we add sequences
  // directly. TODO: handle self-dupes
  if (target == NULL || genome == _root)
  {
    SGSequence* ref = createSGSequence(
      sequence, globalStart - sequence->getStartPosition(),
      globalEnd - globalStart + 1);
  }
  // only conintue if we're a second genome, or there's a possibility
  // of self dupes in the root.
  // if (genome != _root && globalEnd > globalStart)
  else 
  {
    if (target == NULL)
    {
      // map to self
      target = genome;
      _mapMrca = genome;
    }
    
    // block mapping logic below largely taken from halBlockLiftover.cpp
    SegmentIteratorConstPtr refSeg;
    hal_index_t lastIndex;
    if (genome->getNumTopSegments() > 0)
    {
      refSeg = genome->getTopSegmentIterator();
      lastIndex = (hal_index_t)genome->getNumTopSegments();
    }
    else
    {
      refSeg = genome->getBottomSegmentIterator();
      lastIndex = (hal_index_t)genome->getNumBottomSegments();      
    }
    
    refSeg->toSite(globalStart, false);
    hal_offset_t startOffset = globalStart - refSeg->getStartPosition();
    hal_offset_t endOffset = 0;
    if (globalEnd <= refSeg->getEndPosition())
    {
      endOffset = refSeg->getEndPosition() - globalEnd;
    }
    refSeg->slice(startOffset, endOffset);
  
    assert(refSeg->getStartPosition() ==  globalStart);
    assert(refSeg->getEndPosition() <= globalEnd);
    
    set<MappedSegmentConstPtr> mappedSegments;

    ///// DEBUG
    cout << "Mapping " << sequence->getFullName() << " to "
         << target->getName()
         << "; MRC = " << _mapMrca->getName()
         << "; ROOT = " << _root->getName()
         << "; PATH = ";
    for (set<const Genome*>::iterator i = _mapPath.begin();
         i != _mapPath.end(); ++i)
    {
       cout << (*i)->getName() << ", ";
    }
    cout << endl;
    /////
    
    while (refSeg->getArrayIndex() < lastIndex &&
           refSeg->getStartPosition() <= globalEnd)  
    {
      refSeg->getMappedSegments(mappedSegments, target, &_mapPath,
                                true, 0, _root, _mapMrca);
      refSeg->toRight(globalEnd);
    }

    vector<Block*> blocks;
    blocks.reserve(mappedSegments.size());
    SGSide prevHook(SideGraph::NullPos, true);

    vector<MappedSegmentConstPtr> fragments;
    BlockMapper::MSSet emptySet;
    set<hal_index_t> queryCutSet;
    set<hal_index_t> targetCutSet;
    for (set<MappedSegmentConstPtr>::iterator i = mappedSegments.begin();
         i != mappedSegments.end(); ++i)
    {
      BlockMapper::extractSegment(i, emptySet, fragments, &mappedSegments, 
                                  targetCutSet, queryCutSet);
      Block* block = new Block();
      fragmentsToBlock(fragments, *block);
      blocks.push_back(block);
    }
    _lastJoin = NULL;
    // extractSegment() method above works in sorted order by target.
    // we need sorted order by source (to detect insertions, for example).
    sort(blocks.begin(), blocks.end(), BlockPtrLess());

    // we will deal in sequence-relative (as opposed to hals wonky
    // genome-relative) coordinates from here on.
    hal_index_t sequenceStart = globalStart - sequence->getStartPosition();
    hal_index_t sequenceEnd = globalEnd - sequence->getStartPosition();
    
    if (blocks.empty() == true)
    {
      // case with zero mapped blocks.  entire segment will be insertion. 
      processBlock(NULL, NULL, NULL, prevHook, sequence,
                   genome, sequenceStart, sequenceEnd, target);
    }
    else
    {
      // we filter blocks out with cutBlock.  this offset keeps track of prev.
      // todo: we need to put BlockMapper machinery to cut and self-map
      // to handle the reference/insertion paralogy case (maybe).
      size_t cutback = 0;
      for (size_t j = 0; j < blocks.size(); ++j)
      {
        Block* prev = j == 0 ? NULL : blocks[j-1-cutback];
        Block* next = j == blocks.size() - 1 ? NULL : blocks[j+1];
        Block* block = cutBlock(prev, blocks[j]);

        cutback = block == NULL ? cutback + 1 : 0;
        if (block != NULL)
        {
          processBlock(prev, block, next, prevHook, sequence,
                        genome, sequenceStart, sequenceEnd, target);
        }
      }
      // add insert at end / last step in path
      processBlock(blocks.back(), NULL, NULL, prevHook, sequence,
                   genome, sequenceStart, sequenceEnd, target);
    }
    for (size_t j = 0; j < blocks.size(); ++j)
    {
      delete blocks[j];
    }

    if (_lastJoin != NULL)
    {
      _sg->addJoin(_lastJoin);
      _lastJoin = NULL;
    }
  }
}


void SGBuilder::processBlock(Block* prevBlock,
                             Block* block,
                             Block* nextBlock,
                             SGSide& prevHook,
                             const Sequence* srcSequence,
                             const Genome* srcGenome,
                             hal_index_t sequenceStart,
                             hal_index_t sequenceEnd,
                             const Genome* tgtGenome)
{
  hal_index_t prevSrcPos;  // global hal coord of end of last block
  hal_index_t srcPos;  // global hal coord of beginning of block
  if (prevBlock == NULL)
  {
    // minus one here because we consider prevSrcPos covered and want
    // to insert stuff that comes after. 
    prevSrcPos = sequenceStart - 1;
  }
  else
  {
    prevSrcPos = prevBlock->_srcEnd;
  }
  if (block != NULL)
  {
    srcPos = block->_srcStart;
  }
  else
  {
    srcPos = sequenceEnd + 1;
  }

  cout << endl << "PREV " << prevBlock << endl;
  cout << "CUR  " << block << endl;
  cout << "prevSrcPos " << prevSrcPos << " srcPos " << srcPos << endl;

  if (prevBlock == 0)
  {
    _pathLength = 0;
  }

  if (srcPos > prevSrcPos + 1)
  {    
    // handle insertion (source sequence not mapped to target)
    // insert new sequence for gap between prevBock and block
    SGSequence* insertSeq = createSGSequence(srcSequence, prevSrcPos + 1, 
                                              srcPos - prevSrcPos - 1);
    
    // join it on end of last inserted side graph position
    SGSide seqHook(SGPosition(insertSeq->getID(), 0), false);
    if (prevHook.getBase() != SideGraph::NullPos)
    {
      createSGJoin(prevHook, seqHook);
    }

    // our new hook is the end of this new sequence
    prevHook.setBase(SGPosition(insertSeq->getID(),
                                insertSeq->getLength() - 1));
    prevHook.setForward(true);
    _pathLength += srcPos - prevSrcPos - 1;
  }
  if (block != NULL)
  {
    sg_int_t blockLength = block->_srcEnd - block->_srcStart + 1;

    // We want to find the two sides in our Side Graph where the current
    // block's end points map to.  We have to be careful because it's a
    // 2-step mapping (block.src -> block.tgt -> side graph), so need to
    // account for double-reversal case.
    pair<SGSide, SGSide> blockEnds;

    // map from src to target (first mapping);
    SGPosition halTgtFirst(
      (sg_seqid_t)block->_tgtSeq->getArrayIndex(),
      block->_reversed ? block->_tgtEnd : block->_tgtStart);
    
    // map from tgt to side graph (second mapping);
    GenomeLUMap::iterator lui = _luMap.find(
      block->_tgtSeq->getGenome()->getName());
    assert(lui != _luMap.end());
    blockEnds.first = lui->second->mapPosition(halTgtFirst);
    bool sgMapReversed = !blockEnds.first.getForward();
    blockEnds.second = blockEnds.first;
    SGPosition secondPos = blockEnds.second.getBase();
    if (sgMapReversed != block->_reversed)
    {
      secondPos.setPos(secondPos.getPos() - blockLength + 1);
      blockEnds.second.setBase(secondPos);
      blockEnds.first.setForward(true);
      blockEnds.second.setForward(false);
    }
    else
    {
      secondPos.setPos(secondPos.getPos() + blockLength - 1);
      blockEnds.second.setBase(secondPos);
      blockEnds.first.setForward(false);
      blockEnds.second.setForward(true);
    }

    // we map our source block to the interval in the lookup
    // we add the source segment to our lookup structure to map
    // it back to side graph.  this lookup structure is snp
    // ignorant -- ie snp information kept in separate mapping structure
    // at least for now.
    SGPosition newHalPosition((sg_seqid_t)block->_srcSeq->getArrayIndex(),
                              block->_srcStart);
    SGPosition tgtHalPosition((sg_seqid_t)block->_tgtSeq->getArrayIndex(),
                              block->_tgtStart);

    _lookup->addInterval(newHalPosition, tgtHalPosition, blockLength,
                         block->_reversed);

    // we've found how our block fits into the graph (blockEnds).  Now
    // we need to map the inside of the block.  this means processing
    // all the snps, as well as making sure the lookup structure is
    // updated.
    pair<SGSide, SGSide> tempCheck = blockEnds;
    if (_noSubMode == false)
    {
      mapBlockSnps(block, blockEnds);
    }
    cout << "BE " << blockEnds.first << ", " << blockEnds.second << endl;
    cout << "TC " << tempCheck.first << ", " << tempCheck.second << endl;
    assert(tempCheck == blockEnds);

    _pathLength += blockLength;
    assert(blockEnds.first.lengthTo(blockEnds.second) == blockLength);
    
    if (prevHook.getBase() != SideGraph::NullPos)
    {
      // we can have a case where a block aligns identical segments
      // (only when looking for dupes in reference).  do not want to
      // add such self joins here
      if (isSelfBlock(*block) == false)
      {
        // we now know enough to join to prevBlock
        createSGJoin(prevHook, blockEnds.first);
      }
    }
      
    // our new hook is the end of the last join
    prevHook = blockEnds.second;
  }    
  if (block == NULL)
  {
    // note to self, this shoudl be in createSGJoin or
    // somehow centralized. 
    if (_lastJoin != NULL)
    {
      _sg->addJoin(_lastJoin);
      _lastJoin = NULL;
    }
  }
}

void SGBuilder::mapBlockSnps(const Block* block,
                             pair<SGSide, SGSide>& blockEnds)
{  
  // now we're going to slice the block up according to snps.  slices will
  // be runs of positions that either are all snps or all not snps.
  // for each slice, we need to
  hal_index_t length = block->_srcEnd - block->_srcStart + 1;
  string srcDNA;
  string tgtDNA;
  block->_srcSeq->getSubString(srcDNA, block->_srcStart, length);
  block->_tgtSeq->getSubString(tgtDNA, block->_tgtStart, length);

  SGSide hook(blockEnds.first);
  bool sgForwardMap = blockEnds.first <= blockEnds.second; 
  bool runningSnp = false;

  hal_index_t srcPos;
  hal_index_t tgtPos;
  char srcVal;
  char tgtVal;
  hal_index_t bp = 0;
  for (hal_index_t i = 0; i < length; ++i)
  {
    srcPos =  block->_srcStart + i;
    srcVal = srcDNA[i];
    if (block->_reversed == false)
    {
      tgtPos = block->_tgtStart + i;
      tgtVal = tgtDNA[i];
    }
    else
    {
      tgtPos = block->_tgtEnd - i;
      tgtVal = reverseComplement(tgtDNA[tgtDNA.size() - 1 - i]);
    }
    bool snp = isSubstitution(srcVal, tgtVal);
    if (snp == true)
    {
      cout << "snp " << srcVal << "->" << tgtVal << " at srcpos "
           << srcPos << " tgt[ops " << tgtPos << " rev " << block->_reversed
           <<endl;
      cout << "srcDNA " << srcDNA << endl;
      cout << "tgtDNA " << tgtDNA << endl;

    }
    assert(snp == false);
    if (i > 0 && snp != runningSnp)
    {
      pair<SGSide, SGSide> fragEnds = mapSliceSnps(block, bp, i - 1, snp, hook,
                                                   sgForwardMap, srcDNA,
                                                   tgtDNA);
      if (bp == 0)
      {
        blockEnds.first = fragEnds.first;
      }
      hook = fragEnds.second;
      bp = i;
    }
    if (i == length - 1)
    {
      pair<SGSide, SGSide> fragEnds = mapSliceSnps(block, bp, i, snp, hook,
                                                   sgForwardMap, srcDNA,
                                                   tgtDNA);
      if (bp == 0)
      {
        blockEnds.first = fragEnds.first;
      }
      blockEnds.second = fragEnds.second;
      hook = fragEnds.second;
    }
    runningSnp = snp;
  }
}

pair<SGSide, SGSide> SGBuilder::mapSliceSnps(const Block* block,
                                             hal_index_t srcStartOffset,
                                             hal_index_t srcEndOffset,
                                             bool snp, SGSide& hook,
                                             bool sgForwardMap,
                                             const string& srcDNA,
                                             const string& tgtDNA)
{
  //cout << "slice " << srcStartOffset << " - " << srcEndOffset;
  // todo: we need to handle snps here. 

  SGSide start(hook);
  SGPosition pos = start.getBase();
  pos.setPos(pos.getPos() + (srcStartOffset == 0 ? 0 : 1));
  start.setBase(pos);
  pair<SGSide, SGSide> sliceEnds(start, start);
  
  if (sgForwardMap)
  {
    pos.setPos(pos.getPos() + (srcEndOffset - srcStartOffset));
  }
  else
  {
    pos.setPos(pos.getPos() - (srcEndOffset - srcStartOffset));
  }
  sliceEnds.second.setBase(pos);
  sliceEnds.second.setForward(sgForwardMap);
  cout << " -> " << sliceEnds.second << endl;
  return sliceEnds;
}





void SGBuilder::fragmentsToBlock(const vector<MappedSegmentConstPtr>& fragments,
                                 Block& block) const
{
  block._tgtStart = min(min(fragments.front()->getStartPosition(), 
                            fragments.front()->getEndPosition()),
                        min(fragments.back()->getStartPosition(),
                            fragments.back()->getEndPosition()));
  block._tgtEnd = max(max(fragments.front()->getStartPosition(), 
                          fragments.front()->getEndPosition()),
                      max(fragments.back()->getStartPosition(),
                          fragments.back()->getEndPosition()));
  block._tgtSeq = fragments.front()->getSequence();
  assert(block._tgtStart <= block._tgtEnd);

  SlicedSegmentConstPtr srcFront = fragments.front()->getSource();
  SlicedSegmentConstPtr srcBack = fragments.back()->getSource();

  block._srcStart =  min(min(srcFront->getStartPosition(), 
                             srcFront->getEndPosition()),
                         min(srcBack->getStartPosition(),
                             srcBack->getEndPosition()));
  block._srcEnd = max(max(srcFront->getStartPosition(), 
                          srcFront->getEndPosition()),
                      max(srcBack->getStartPosition(),
                          srcBack->getEndPosition()));
  block._srcSeq = srcFront->getSequence();
  assert(block._srcStart <= block._srcEnd);
  
  // convert to segment level;
  block._tgtStart -= block._tgtSeq->getStartPosition();
  block._tgtEnd -= block._tgtSeq->getStartPosition();
  block._srcStart -= block._srcSeq->getStartPosition();
  block._srcEnd -= block._srcSeq->getStartPosition();

  block._reversed = srcFront->getReversed() != fragments.front()->getReversed();
}

SGBuilder::Block* SGBuilder::cutBlock(Block* prev, Block* cur)
{
  if (prev != NULL)
  {
    //cout << "cPrev " << (size_t)prev << " " << prev << endl
    //     << "cCur " << (size_t)cur << " " << cur << endl;
    assert(prev->_srcStart <= prev->_srcEnd);
    assert(cur->_srcStart <= cur->_srcEnd);
    sg_int_t cutLen = prev->_srcEnd - cur->_srcStart + 1;
    if (cutLen > 0)
    {
      assert(false);
      cur->_srcStart += cutLen;
      cur->_tgtStart += cutLen;
      
      if (cur->_srcStart > cur->_srcEnd)
      {
        cur = NULL;
      }
    }
  }
  if (cur != NULL)
  {
    assert(cur->_srcStart <= cur->_srcEnd);
    assert(cur->_tgtStart <= cur->_tgtEnd);
  }
  //cout << "cCuO " << (size_t)cur << " " << cur << endl;

  return cur;
}

bool SGBuilder::verifyPath(const Sequence* sequence,
                           const vector<SGSegment>& path) const
{
  string pathString;
  string buffer;
  
  for (size_t i = 0; i < path.size(); ++i)
  {
    const SGSegment& seg = path[i];
    const SGSequence* seq = _sg->getSequence(
      seg.getSide().getBase().getSeqID());
    getSequenceString(seq, buffer, seg.getMinPos().getPos(), seg.getLength());
    if (seg.getSide().getForward() == false)
    {
      reverseComplement(buffer);
    }
    pathString.append(buffer);
    if (i > 0)
    {
      SGSide srcSide = path[i-1].getOutSide();
      SGSide tgtSide = seg.getInSide();
      SGJoin queryJoin(srcSide, tgtSide);      
      const SGJoin* join = _sg->getJoin(&queryJoin);
      assert(join != NULL);
      if (join == NULL)
      {
        return false;
      }
    }
  }

  if (_noSubMode == true && sequence->getGenome()->getParent() == NULL)
  {
    getRootSubString(buffer, sequence, 0, sequence->getSequenceLength());
  }
  else
  {
    sequence->getString(buffer);
  }

  if (buffer != pathString)
  {
    cout << sequence->getFullName() << endl;
    cout << "BUF " << buffer.length()   << endl;
    cout << "PAT " << pathString.length()  << endl;
    
    for (size_t x = 0; x < buffer.length(); ++x)
    {
      if (buffer[x] != pathString[x])
      {
        cout << x << " " << buffer[x] << "->" << pathString[x] << endl;
        break;
      }
    }
    cout << endl;
  }
  assert(buffer == pathString);
  if (buffer != pathString)
  {
    return false;
  }

  return true;
}


void SGBuilder::getRootSubString(string& outDNA, const Sequence* sequence,
                                 hal_index_t pos, hal_index_t length) const
{
  string buffer;
  outDNA.erase();
  // copy the DNA sequence up from the children for the entire genome. 
  if (_rootString.length() == 0)
  {
    const Genome* root = sequence->getGenome();
    assert (root->getParent() == NULL);
    BottomSegmentIteratorConstPtr bottom = root->getBottomSegmentIterator();
    TopSegmentIteratorConstPtr top = root->getChild(0)->getTopSegmentIterator();
    for (size_t i = 0 ; i < root->getNumBottomSegments(); ++i)
    {
      for (size_t j = 0; j < root->getNumChildren(); ++j)
      {
        if (bottom->hasChild(j))
        {
          top->toChild(bottom, j);
          top->getString(buffer);
          _rootString.append(buffer);
          break;
        }
        assert(j != root->getNumChildren() - 1);
      }
      bottom->toRight();
    }
    assert(root->getSequenceLength() == _rootString.length());
  }

  // do our query on this new root.
  outDNA = _rootString.substr(sequence->getStartPosition() + pos, length);
}
