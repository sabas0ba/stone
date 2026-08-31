#!/usr/bin/perl
# sfs4 の pack 実装。
#
# sfs3.sh の shell 実装は小さい tree には十分だが、entry ごとに stat / awk /
# dd を起動するため、81,355 paths の GCC 4.7.4 には使えない。GNU find から
# metadata を NUL 区切りで1回だけ受け取り、tableとdataを単一processで書く。
use strict;
use warnings;
use bytes;

use constant {
    TBLOFF  => 32,
    ENTSZ   => 152,
    NAMEMAX => 127,
    F_USED  => 1,
    F_DIR   => 2,
};

sub fail {
    die "sfs4.sh: $_[0]\n";
}

sub field {
    my ($fh) = @_;
    my $value = readline($fh);
    defined $value or fail("truncated metadata from find");
    chop $value;  # NUL separator
    return $value;
}

sub mtime_ns {
    my ($value) = @_;
    $value =~ /\A(\d+)\.(\d+)/ or fail("bad mtime from find: $value");
    my ($seconds, $fraction) = ($1, $2);
    $fraction .= "0" x (9 - length($fraction)) if length($fraction) < 9;
    $fraction = substr($fraction, 0, 9);
    return int($seconds) * 1_000_000_000 + int($fraction);
}

sub depth {
    my ($path) = @_;
    return 0 if $path eq "";
    return 1 + ($path =~ tr{/}{/});
}

sub parent_and_name {
    my ($path) = @_;
    my $slash = rindex($path, "/");
    return ("", $path) if $slash < 0;
    return (substr($path, 0, $slash), substr($path, $slash + 1));
}

sub entry {
    my ($name, $parent, $offset, $length, $flags, $mtime) = @_;
    length($name) <= NAMEMAX or fail("name too long (" . length($name) . " bytes): $name");
    return $name . ("\0" x (128 - length($name)))
        . pack("V4Q<", $parent, $offset, $length, $flags, $mtime);
}

sub copy_file {
    my ($source, $output) = @_;
    open my $input, "<:raw", $source or fail("cannot open $source: $!");
    my $buffer;
    while (1) {
        my $read = sysread($input, $buffer, 1024 * 1024);
        defined $read or fail("cannot read $source: $!");
        last if $read == 0;
        my $written = 0;
        while ($written < $read) {
            my $n = syswrite($output, $buffer, $read - $written, $written);
            defined $n && $n > 0 or fail("cannot write image: $!");
            $written += $n;
        }
    }
    close $input or fail("cannot close $source: $!");
}

@ARGV == 4 or fail("usage: sfs4.sh pack <dir> <img> <size> <maxent>");
my ($directory, $image, $size, $capacity) = @ARGV;
-d $directory or fail("no such directory: $directory");
$size =~ /\A\d+\z/ && $size > 0 && $size <= 0xffffffff
    or fail("bad image size: $size");
$capacity =~ /\A\d+\z/ && $capacity > 0
    or fail("bad entry capacity: $capacity");

my @records;
open my $find, "-|", "find", $directory, "-mindepth", "0", "-printf",
    "%P\\0%y\\0%s\\0%T@\\0" or fail("cannot run find: $!");
local $/ = "\0";
while (1) {
    my $path = <$find>;
    last unless defined $path;
    chop $path;
    my $type = field($find);
    my $length = field($find);
    my $mtime = field($find);
    ($type eq "d" || $type eq "f")
        or fail("unsupported entry type: $directory/$path");
    $length =~ /\A\d+\z/ && $length <= 0xffffffff
        or fail("file too large: $directory/$path");
    push @records, {
        path   => $path,
        type   => $type,
        length => int($length),
        mtime  => mtime_ns($mtime),
    };
}
close $find or fail("find failed");

my ($root) = grep { $_->{path} eq "" && $_->{type} eq "d" } @records;
defined $root or fail("root directory is missing from find output");
my @directories = sort {
    depth($a->{path}) <=> depth($b->{path}) || $a->{path} cmp $b->{path}
} grep { $_->{path} ne "" && $_->{type} eq "d" } @records;
my @files = sort { $a->{path} cmp $b->{path} }
    grep { $_->{type} eq "f" } @records;
my @ordered = ($root, @directories, @files);
@ordered <= $capacity
    or fail("too many entries (need " . scalar(@ordered) . ", max $capacity)");

my $data_start = TBLOFF + $capacity * ENTSZ;
$data_start < $size or fail("size too small for $capacity entries");
my $cursor = $data_start;
my %index = ("" => 0);
my $table = "";
for (my $i = 0; $i < @ordered; $i++) {
    my $record = $ordered[$i];
    my ($parent_path, $name) = parent_and_name($record->{path});
    my $parent = $i == 0 ? 0 : $index{$parent_path};
    defined $parent or fail("parent not found: $parent_path (for $record->{path})");
    my $flags = F_USED | ($record->{type} eq "d" ? F_DIR : 0);
    my $offset = $record->{type} eq "f" ? $cursor : 0;
    my $length = $record->{type} eq "f" ? $record->{length} : 0;
    my $next_cursor = $record->{type} eq "f"
        ? int(($cursor + $length + 3) / 4) * 4
        : $cursor;
    $next_cursor <= $size or fail("image full at: $record->{path}");
    $table .= entry($name, $parent, $offset, $length, $flags, $record->{mtime});
    $record->{offset} = $offset;
    $index{$record->{path}} = $i if $record->{type} eq "d";
    $cursor = $next_cursor;
}

open my $output, ">:raw", $image or fail("cannot create $image: $!");
truncate($output, $size) or fail("cannot resize $image: $!");
print {$output} pack("a4V4", "sfs4", $size, TBLOFF, $capacity, $data_start)
    . ("\0" x 12) or fail("cannot write superblock: $!");
print {$output} $table or fail("cannot write table: $!");

my $done = 0;
for my $record (@files) {
    seek($output, $record->{offset}, 0) or fail("cannot seek image: $!");
    copy_file("$directory/$record->{path}", $output) if $record->{length} > 0;
    $done++;
    if ($ENV{SFS4_PROGRESS} && $done % 1000 == 0) {
        print STDERR "sfs4: copied $done/" . scalar(@files) . " files\n";
    }
}
seek($output, 16, 0) or fail("cannot seek superblock: $!");
print {$output} pack("V", $cursor) or fail("cannot write cursor: $!");
close $output or fail("cannot close $image: $!");

if ($ENV{SFS4_PROGRESS}) {
    print STDERR "sfs4: packed " . scalar(@ordered) . "/$capacity entries "
        . "(cursor=$cursor, done)\n";
}
