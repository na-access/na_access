#!/usr/bin/perl
#______________________________________________________________________________
#	Author Jens Bornemann, 02/23/2005, jbornema@netapp.com
#	
#	na_access_files.pl
#	
#	Creates statistic files from a Symantec Scan Engine report.
#	The input file must be of format:
#	YYYY/MM/DD-hh:mm:ss,?,"Desciption in some words. -->  \\hostname\ONTAP_ADMIN$\vol\home\..."
#	
#	Parameter : "directory"
#	directory : All files in this directory and its sub directories are accessed
#	
#	© 2005 Network Appliance
#______________________________________________________________________________

use strict;
use Config;
use threads;

my $thread_exit;
my $thread_count_max = 10;
my $thread_count = 0;
my @files = ();
my $last_files_count = 0;
my %exclude;

#______________________________________________________________________________
#	Show help and exit
#
#______________________________________________________________________________
sub help {
print("
Missing parameter.
$0 \"directory\" [exclude=ext1[,ext2[,..]]] [max threads]

directory : All files in this directory and its sub directories are accessed
[exclude=ext1[,ext2[,..]]] : List of file extension that are not scanned
[max threads] : Maximum number of threads that are used, 10 is default.
");
exit;
}

#______________________________________________________________________________
#	Opens the file and reads 2 bytes.
#______________________________________________________________________________
sub openFile($ ) {
	my ($file) = @_;
	open(FILE, '<', $file); # open file
	read(FILE, my $data, 2); # read 2 bytes
	close(FILE); # close file
    print("$thread_count: $file\n");
}
#______________________________________________________________________________
# Does what should be done to file.
# It places the $file parameter into shared array @files and
# creates threads id needed but not more than $thread_count_max.
#______________________________________________________________________________
sub doFile($ ) {
	my ($file) = @_;
  $file =~ /(.+)\.(.+)/;
  if (!exists $exclude{"\U$2\E"}) {
    {
      lock(@files);
      push(@files, $file);
    }
    # create new thread if needed
    if ($thread_count + 1 < $thread_count_max && ($thread_count == 0 or $last_files_count >= $#files)) {
      threads->new(\&thread);
    }
    $last_files_count = $#files;
  }
}
#______________________________________________________________________________
# Thread method that's calling openFile()
#______________________________________________________________________________
sub thread() {
  $thread_count++;
  while ($#files > 0 or !$thread_exit) {
    while ($#files < 1 and !$thread_exit) {
      threads->yield();
    }
    my $file;
    {
      lock(@files);
      $file = pop(@files) if ($#files > 0);
    }
    openFile($file) if ($file);
  }
}

#______________________________________________________________________________
#	Calls openFile() on all files in this directory.
#______________________________________________________________________________
sub allFiles($ ) {
	my ($dir) = @_;
	$dir =~ s/\\/\//g; # convert \ to /
	$dir .= '/' if (substr($dir, length($dir) - 1, 1) ne '/');
	if (opendir(DIR, $dir)) {
		my @entries = readdir(DIR);
		foreach my $entry (@entries) {
			# skip '.' and '..'
			next if ($entry eq '.' or $entry eq '..');
      if (-f $dir.$entry) {
        # do what should be done one the file
        doFile($dir.$entry);
			} elsif (-d $dir.$entry) {
				# directory found recursive call
				allFiles($dir.$entry);
			}
		}
		closedir(DIR);
	}
}

#______________________________________________________________________________
#	Main
#______________________________________________________________________________

# check that threading is supported
$Config{useithreads} or die "Recompile Perl with threads to run this program.";

# show help if parameter missing
help() if ($#ARGV < 0);

# load options
my @values;
foreach my $opt (@ARGV) {
  @values = split /,d*/, $1 if ($opt =~ /exclude=(.*)/);
  $thread_count_max = $opt if ($opt =~ /\d+/);
}
# fill exclude list
if (@values) {
  foreach my $ext (@values) {
    $exclude{"\U$ext\E"} = 0;
  }
}

# access all files in directory
allFiles($ARGV[0]);

# exit all thread
$thread_exit = 1;

# join all threads
foreach my $thread (threads->list()) {
  $thread->join();
}


