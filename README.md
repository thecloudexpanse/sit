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

---

## macbinfilt

**Filter and reassemble BinHex files from Usenet**

`macbinfilt` is a companion utility for processing Mac binary files posted to Usenet newsgroups, particularly comp.binaries.mac. In the 1980s and 1990s, Mac software was commonly distributed through Usenet in BinHex 4.0 format, often split across multiple article parts.

**What it does:**

- Filters Usenet article text to extract BinHex-encoded data
- Automatically reorders multi-part articles if parts arrive out of sequence
- Strips extraneous headers, signatures, and non-BinHex content
- Outputs clean BinHex data ready for decoding with `xbin` or similar tools

**Usage:**

```bash
# Process a single article or file
macbinfilt article.txt > output.hqx

# Process multiple parts (will reorder automatically)
macbinfilt part1.txt part3.txt part2.txt > complete.hqx

# Process from stdin (e.g., from a newsreader)
cat article.txt | macbinfilt > output.hqx
```

**When to use it:**

- You're working with historical Mac software archives from Usenet
- You need to extract BinHex data from articles with headers and footers
- You received multi-part articles out of order and need them reassembled
- You're processing comp.binaries.mac archives

**Notes:**

- `macbinfilt` looks for lines containing the text "part N of M" to identify multi-part files
- Missing parts will be reported as errors
- Output always begins with the BinHex signature: `(This file must be converted with BinHex 4.0)`
- The filtered output can then be decoded using BinHex decoders like `xbin`

