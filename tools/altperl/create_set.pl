#!/usr/bin/perl
# $Id: create_set.pl,v 1.1 2004-07-25 04:02:50 cbbrowne Exp $
# Author: Christopher Browne
# Copyright 2004 Afilias Canada
require 'slon-tools.pm';
require 'slon.env';
my ($set) = @ARGV;
if ($set =~ /^set(\d+)$/) {
  $set = $1;
} else {
  print "Need set identifier\n";
  die "create_set.pl setN\n";
}

$OUTPUTFILE="/tmp/add_tables.$$";

open (OUTFILE, ">$OUTPUTFILE");
print OUTFILE genheader();

foreach my $table (@SERIALTABLES) {
 print OUTFILE "
		echo '  Adding unique key to table public.$table...';
		table add key (
		    node id=1,
		    full qualified name='public.$table'
		);
";
}
close OUTFILE;
print `slonik < $OUTPUTFILE`;

open (OUTFILE, ">$OUTPUTFILE");
print OUTFILE genheader();

print OUTFILE "
	try {
		create set (id = $set, origin = 1, comment = 'Set for slony tables');
	}
	on error {
		echo 'Could not create subscription set!';
		exit -1;
	}
";

close OUTFILE;
print `slonik < $OUTPUTFILE`;

open (OUTFILE, ">$OUTPUTFILE");
print OUTFILE genheader();
print OUTFILE "
	echo 'Subscription set created';
	echo 'Adding tables to the subscription set';

";

$TABLE_ID=1;
foreach my $table (@SERIALTABLES) {
  if ($table =~ /^(.*\..*)$/) {
    # Table has a namespace specified
  } else {
    $table = "public.$table";
  }
    print OUTFILE "
		set add table (set id = $set, origin = 1, id = $TABLE_ID, full qualified name = '$table', comment = 'Table public.$table', key=serial);
                echo 'Add unkeyed table $table';
"; 
  $TABLE_ID++;
}

foreach my $table (@KEYEDTABLES) {
  if ($table =~ /^(.*\..*)$/) {
    # Table has a namespace specified
  } else {
    $table = "public.$table";
  }
  print OUTFILE "
		set add table (set id = $set, origin = 1, id = $TABLE_ID, full qualified name = '$table', comment = 'Table public.$table');
                echo 'Add keyed table $table';
";
  $TABLE_ID++;
}

close OUTFILE;
print `slonik < $OUTPUTFILE`;

open (OUTFILE, ">$OUTPUTFILE");
print OUTFILE genheader();
# Finish subscription set...
print OUTFILE "
                echo 'Adding sequences to the subscription set';
";

$SEQID=1;
foreach my $seq (@SEQUENCES) {
  if ($seq =~ /^(.*\..*)$/) {
    # Table has a namespace specified
  } else {
    $seq = "public.$seq";
  }
  print OUTFILE "
                set add sequence (set id = $set, origin = 1, id = $SEQID, full qualified name = '$seq', comment = 'Sequence public.$seq');
                echo 'Add sequence $seq';
";
  $SEQID++;
}
print OUTFILE "
        echo 'All tables added';
";

print `slonik < $OUTPUTFILE`;
unlink($OUTPUTFILE);
