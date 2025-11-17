# sit
**Create StuffIt archives on Unix systems**

**Summary**

`sit` is a command-line tool that can create StuffIt 1.5.1-compatible archives containing files or folders. Resource forks and metadata are preserved, and the resulting archive can be extracted with StuffIt or StuffIt Expander on a classic Mac OS system. Run the tool in Terminal with no arguments to see its options.

**Introduction**

The `sit` program was originally written in 1988 and posted to Usenet's comp.sources.mac newsgroup by Tom Bereiter. The program assumed that it was running on a Unix system where each input file either had no resource information at all, or was split into 3 binary files named with .data, .rsrc, and .info extensions.

This modern update of `sit` takes into account the fact that macOS has been a Unix system since 2001, and that input files may have resource forks. The program has been enhanced to accept either files or directories as input, and will archive the contents of directories recursively.

**Usage**

    sit [-v] [-u] [-T type] [-C creator] [-o dstfile] file ...

Creates a StuffIt 1.5.1-compatible archive from one or more files (or folders) specified as arguments. The default output file is "archive.sit" if the `-o` option is not provided. Use `-v`, `-vv`, or `-vvv` to see increasingly verbose output.

Files without a resource fork are assigned the default type `TEXT` and creator `KAHL`, identifying them as a text file created by THINK C. You can override the default type and creator with the `-T` and `-C` options.

The `-u` option converts all linefeeds (`'\n'`) to carriage returns (`'\r'`) in the data fork of the file. This is really only useful when archiving plain Unix text files which you intend to open in a classic Mac application like SimpleText or MacWrite. In general, you should avoid this option, especially if you are archiving other types of documents or applications.

**Examples**

	# create "archive.sit" containing three specified files
	sit file1 file2 file3
	# create "FolderArchive.sit" containing FolderToBeArchived
	sit -o FolderArchive.sit FolderToBeArchived
	# specify that untyped files are JPEG and open in GraphicConverter
	sit -o jpgArchive.sit -T JPEG -C GKON *.jpg

**Limitations**

Unlike the original `sit`, this program does not currently compress data. Its primary purpose is to create a portable container that can be safely transferred from a modern system to a classic Mac computer or emulator. Re-adding compression is being investigated for a future update.

This program is known to compile and run on macOS systems (Snow Leopard 10.6 or later). While it is intended that the software should be able to compile and run on any UNIX system, it currently does not work with certain systems whose stat structure does not contain a `st_birthtime` field, as this is not yet part of a POSIX standard. Also, it has not been tested on Linux. This is also being investigated for a future update.

This software may contain bugs. Use at your own risk.

**Linux and Cross-Platform Support**

This version of `sit` has been enhanced to work on Linux and other Unix systems. On non-macOS platforms, resource forks and file metadata can be provided using the AppleDouble file format in both `._filename` or `.rsrc` sidecars.

**Building**

This is a bare-bones "C" command-line tool. With Xcode's CLTools support installed on macOS, you should be able to build the tool by typing `make` while the `sit` directory is the current directory.

