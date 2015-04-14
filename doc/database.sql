-- We have our FASTA file right next to this file, so we can reference it by
-- relative path. Probably.
INSERT INTO FASTA VALUES (1, 'file://sequence.fa');

-- We define all the sequences in that file. Get hashes by removing all line
-- breaks and whitespace at <http://removelinebreaks.net/>, then taking the md5
-- with <http://onlinemd5.com/>, and finally converting the hash to lower case
-- with <http://convertcase.net/>
INSERT INTO Sequence VALUES (1, 1, 'chr1', 'f28788f2203d71311b5d8fe81fe1bd2e', 1000);
INSERT INTO Sequence VALUES (2, 1, 'chr1snp1', '7fc56270e7a70fa81a5935b72eacbe29', 1);
INSERT INTO Sequence VALUES (3, 1, 'chr2', '2895d94c7491966ef9df7af5ecf77e9f', 1000);
-- IDs won't always be contiguous and in order
INSERT INTO Sequence VALUES (8, 1, 'chr2ins1', '4833b4fa1627b1ee25f83698f768f997', 30);
INSERT INTO Sequence VALUES (5, 1, 'chr3', '52ae3ef016c60c5e978306e8d3334cd8', 1000);

-- And some graph topology
-- Attach the SNP on chr1 at base 80
INSERT INTO GraphJoin VALUES (1, 1, 79, 'FALSE', 2, 0, 'TRUE');
INSERT INTO GraphJoin VALUES (2, 1, 81, 'TRUE', 2, 0, 'FALSE');
-- Add a tandem duplication around it.
INSERT INTO GraphJoin VALUES (3, 1, 59, 'TRUE', 1, 74, 'FALSE');
-- Add a deletion on chr2
INSERT INTO GraphJoin VALUES (4, 3, 33, 'FALSE', 3, 108, 'TRUE');
-- Add a translocation from chr1 to chr2, which is just a pure insertion in chr2
INSERT INTO GraphJoin VALUES (5, 1, 233, 'TRUE', 3, 310, 'FALSE');
INSERT INTO GraphJoin VALUES (6, 1, 289, 'FALSE', 3, 311, 'TRUE');
-- Add a 30bp point insert on chr2 at position 300
INSERT INTO GraphJoin VALUES (7, 3, 300, 'FALSE', 8, 0, 'TRUE');
INSERT INTO GraphJoin VALUES (8, 3, 301, 'TRUE', 8, 29, 'FALSE');
-- Nothing needs to touch chr3.

-- Make a ReferenceSet. 
INSERT INTO ReferenceSet VALUES (1, NULL, NULL, 1, 'normal', 'FALSE');
-- Make references, but wth IDs in a different order than the sequences. They
-- all span their sequences, so they don't need lengths or md5 checksums set.
INSERT INTO Reference VALUES (1, 'chr1', date('now'), 1, 0, NULL, NULL, 'FALSE', NULL, NULL, 'TRUE');
INSERT INTO Reference VALUES (2, 'chr2', date('now'), 3, 0, NULL, NULL, 'FALSE', NULL, NULL, 'TRUE');
INSERT INTO Reference VALUES (3, 'chr3', date('now'), 5, 0, NULL, NULL, 'FALSE', NULL, NULL, 'TRUE');
INSERT INTO Reference VALUES (4, 'chr1snp1', date('now'), 2, 0, NULL, NULL, 'FALSE', NULL, NULL, 'FALSE');
INSERT INTO Reference VALUES (5, 'chr2ins1', date('now'), 8, 0, NULL, NULL, 'FALSE', NULL, NULL, 'FALSE');
-- Add them to the set
INSERT INTO Reference_ReferenceSet_Join VALUES (1, 1);
INSERT INTO Reference_ReferenceSet_Join VALUES (2, 1);
INSERT INTO Reference_ReferenceSet_Join VALUES (3, 1);
INSERT INTO Reference_ReferenceSet_Join VALUES (4, 1);
INSERT INTO Reference_ReferenceSet_Join VALUES (5, 1);

-- Now we need to describe some inputs we pretend we used to make the graph.

-- First declare a VariantSet. TODO: Why does it need a VCF URI?
INSERT INTO VariantSet VALUES (1, 1, 'file:///dev/null');

-- Declare three Alleles, one for each input we want to describe, in the
-- VariantSet.
INSERT INTO Allele VALUES (1, 1);
INSERT INTO Allele VALUES (2, 1);
INSERT INTO Allele VALUES (3, 1);

-- The first path includes the SNP on chr1.
INSERT INTO AllelePathItem VALUES (1, 1, 1, 0, 80, 'TRUE');
INSERT INTO AllelePathItem VALUES (1, 2, 2, 0, 1, 'TRUE');
INSERT INTO AllelePathItem VALUES (1, 3, 1, 81, 919, 'TRUE');

-- The second path is mostly chr2 but takes the translocation and includes part
-- of chr1.
INSERT INTO AllelePathItem VALUES (2, 1, 3, 0, 311, 'TRUE');
INSERT INTO AllelePathItem VALUES (2, 2, 1, 233, 57, 'TRUE');
INSERT INTO AllelePathItem VALUES (2, 3, 3, 311, 689, 'TRUE');

-- The third is all of chr1.
INSERT INTO AllelePathItem VALUES (3, 1, 1, 0, 1000, 'TRUE');

-- Now we have CallSets for all these paths
INSERT INTO CallSet VALUES (1, 'First Input', 'UCSC01');
INSERT INTO CallSet VALUES (2, 'Second Input', 'UCSC02');
INSERT INTO CallSet VALUES (3, 'Third Input', 'UCSC03');

-- Put them all in the VariantSet
INSERT INTO VariantSet_CallSet_Join VALUES (1, 1);
INSERT INTO VariantSet_CallSet_Join VALUES (1, 2);
INSERT INTO VariantSet_CallSet_Join VALUES (1, 3);

-- Now call the Alleles, with no Variants and ploidy 1 in their assigned
-- samples. TODO: Don't bother calling 0 ploidy for all the other Alleles?
INSERT INTO AlleleCall VALUES (1, 1, NULL, 1);
INSERT INTO AlleleCall VALUES (2, 1, NULL, 0);
INSERT INTO AlleleCall VALUES (3, 1, NULL, 0);

INSERT INTO AlleleCall VALUES (1, 2, NULL, 0);
INSERT INTO AlleleCall VALUES (2, 2, NULL, 1);
INSERT INTO AlleleCall VALUES (3, 2, NULL, 0);

INSERT INTO AlleleCall VALUES (1, 3, NULL, 0);
INSERT INTO AlleleCall VALUES (2, 3, NULL, 0);
INSERT INTO AlleleCall VALUES (3, 3, NULL, 1);

-- Now each of our CallSets has ploidy 1 for the path in the graph corresponding
-- to its associated input sequence.






