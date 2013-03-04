/****************************************************************************************************
 *
 *  na_access_files.js folder
 *
 *  Accesses all files in specified folder and all its subfolders.
 *  The files are opened in read only mode and two bytes are read.
 *
 *  This script triggers the Filer Virusscan for all file in specified folder.
 *
 *  
 *  Author: 02/23/2005, Jens Bornemann, jbornema@netapp.com
 *
 *  © 2005 Network Appliance
 *
 ****************************************************************************************************/
 
var fso = new ActiveXObject("Scripting.FileSystemObject");

/****************************************************************************************************
 * allFiles: calls for all files in directory f and its subdirectories the openFile function
 ****************************************************************************************************/
function allFiles(f) {
	try {
		// open all files
		for (var fc = new Enumerator(f.Files); !fc.atEnd(); fc.moveNext()) {
			var file = fc.item();
			openFile(file);
		}
	} catch (e) {
		// some error accessing the files of the folder
		// no reporting yet
	}

	try {	
		// go into all subfolders
		for (var fc = new Enumerator(f.SubFolders); !fc.atEnd(); fc.moveNext()) {
			var sf = fc.item();
			allFiles(sf);
		}
	} catch (e) {
		// some error accessing the subfolders of the folder
		// no reporting yet
	}
}

/****************************************************************************************************
 * openFile: accesses the file by opening it and reading bytes.
 ****************************************************************************************************/
function openFile(f) {
	try {
		var fs = fso.OpenTextFile(f, 1); // open file for reading
		if (f.Size > 1) {
			var data = fs.Read(2); // read 2 bytes
			//WScript.Echo(f);
		}
		fs.Close(); // close file stream
	} catch (e) {
		// some error opening the file
		// no reporting yet
	}
}

/****************************************************************************************************
 * Main
 ****************************************************************************************************/
var args = WScript.Arguments;
// show help if parameter missing
if (args.length < 1) {
   WScript.Echo("Missing parameter.\nPlease specify the directory to trigger the Filer Virusscan.");
   WScript.Quit(1);
}

// show error if directory is not accessable
if (!fso.FolderExists(args(0))) {
   WScript.Echo("Folder '" + args(0) + "' doesn't exist.");
   WScript.Quit(2);
}

// scan all files
allFiles(fso.GetFolder(args(0)));
