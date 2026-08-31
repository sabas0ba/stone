#!/usr/bin/perl
# sfs4 imageの一覧を出す。table全体を1回で読み、GCC規模でもprocessを
# entryごとに起動しない。
use strict;
use warnings;
use bytes;

use constant {
    ENTSZ  => 152,
    F_USED => 1,
    F_DIR  => 2,
};

sub fail {
    die "sfs4.sh: $_[0]\n";
}

sub read_exact {
    my ($fh, $length) = @_;
    my $data = "";
    while (length($data) < $length) {
        my $n = read($fh, $data, $length - length($data), length($data));
        defined $n or fail("cannot read image: $!");
        $n > 0 or fail("truncated image");
    }
    return $data;
}

@ARGV == 1 or fail("usage: sfs4.sh list <img>");
my ($image) = @ARGV;
open my $input, "<:raw", $image or fail("cannot open $image: $!");
my $header = read_exact($input, 32);
my ($magic, $total, $table_offset, $capacity, $cursor) =
    unpack("a4V4", substr($header, 0, 20));
$magic eq "sfs4" or fail("not an sfs4 image: $image");
my $image_size = -s $input;
$total >= 32 && $total <= $image_size or fail("bad image size in header: $total");
$table_offset >= 32 && $table_offset <= $total or fail("bad table offset: $table_offset");
$capacity > 0 && $capacity <= int(($total - $table_offset) / ENTSZ)
    or fail("bad table capacity: $capacity");
$cursor >= $table_offset + $capacity * ENTSZ && $cursor <= $total
    or fail("bad data cursor: $cursor");

seek($input, $table_offset, 0) or fail("cannot seek table: $!");
my $table = read_exact($input, $capacity * ENTSZ);
my @paths;
for (my $i = 0; $i < $capacity; $i++) {
    my $entry = substr($table, $i * ENTSZ, ENTSZ);
    my $flags = unpack("V", substr($entry, 140, 4));
    next if ($flags & F_USED) == 0;
    my $name = substr($entry, 0, 128);
    $name =~ s/\0.*\z//s;
    my ($parent, $offset, $length) = unpack("V3", substr($entry, 128, 12));
    my $mtime = unpack("Q<", substr($entry, 144, 8));
    if ($i == 0) {
        $parent == 0 && $name eq "" or fail("bad root entry");
        $paths[0] = "";
        next;
    }
    defined $paths[$parent] or fail("parent not found for entry $i");
    my $path = $paths[$parent] eq "" ? $name : "$paths[$parent]/$name";
    $paths[$i] = $path;
    if (($flags & F_DIR) != 0) {
        printf "d %8s %20s %s\n", "-", $mtime, $path;
    } else {
        $offset <= $total && $length <= $total - $offset
            or fail("file outside image: $path");
        printf "f %8s %20s %s\n", $length, $mtime, $path;
    }
}
close $input or fail("cannot close $image: $!");
