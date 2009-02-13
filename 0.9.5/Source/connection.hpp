#ifndef CONNECTIONHPP
#define CONNECTIONHPP
//---------------------------------------------------------------------------------------------
/*
			CONNECTION.HPP
			--------------
			This file contains the definitions of the CONNECTION class and all its 
			member functions.

			Upon creating a new connection the calling function hands the connection
			the Socket Descriptor that the connection is on, and a sockaddr_in 
			structure which contains information about the client. For example:

				CONNECTION * NewRequest = new CONNECTION (SFD, CLA);

			Where SFD is the Socket Descriptor, and CLA is a sockaddr_in structure.
			The constructor then recieves the request from the client, and breaks it up
			into all the possible values that might be used in the connection, such as
			the type of request, file requested, host, MIME types accepted by the client,
			etc. 

			Then, the constructor checks that the file requested by the client exists.
			If it does, it checks if it is a folder or a file. If its a file it gets the
			extension and cross references it with the appropriate MIME type. Then, it 
			checks if the file is a CGI script or a binary file, and sets the flags
			appropriately. If it is just a plain text file, neither flags are set.

			Update:
			A security bug was found which if the user was to use the URL:
			www.anyhost.com/../../windows

			Would list the windows directory. The same theory would work for all directories,
			thus giving full access to a system. 
*/
//---------------------------------------------------------------------------------------------
#include <windows.h>
#include <string>
#include <winsock.h>
#include <map>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <stdio.h>
#include <ctime>
#include "options.hpp"											// Contains definitions of the VHI (Virtual Host Index)

using namespace std;
#pragma comment(lib, "wsock32.lib")								// Link with winsock32
#pragma warning(disable:4786)									// VC++ Can be a bit annoying when it comes to maps, 
																//  so we use this line to get rid of the warnings

// The following is what GetFileAttributes() returns when the file specified
//  does not exist. This should be in windows.h somewhere, but its not. I 
//  had to discover this value for myself. Don't lose it!


#define FILE_INVALID						4294967295
int CGICounter = 0;												// Counter for every CGI script processed

//---------------------------------------------------------------------------------------------
//			Connection class
//---------------------------------------------------------------------------------------------
class CONNECTION
{
  public:
	CONNECTION(int SFD_SET, struct sockaddr_in);				// Constructor
	bool LogConnection();										// Logs connection to the appropriate log
	bool ReadRequest();											// Reads the request and sets values
	bool HandleRequest();										// Handles the request

  private:
	// Methods
	bool SetFileType();											// Sets if the file is a script or binary
	bool IndexFolder();											// Indexes the folder by listing all the files
	bool SendText();											// Sends the requested file if it is text
	bool SendCGI();												// Sends the requested file if it is a script
	bool SendBinary();											// Sends the requested file if it is binary
	bool SendError();											// Outputs the appropriate error code
	bool LogText(string);										// Logs some text. Used only for testing
	string CalculateSize();										// Outputs the file size
	bool ModifiedSince(string Date);							// Was the file modifed since...
	bool UnModifiedSince(string Date);							// Is the file Unmodified since...

	// Properties
	int SFD;													// Socket descriptor of connection
	struct sockaddr_in ClientAddress;							// Client address structure
	VIRTUALHOST *ThisHost;										// This virtual host host

	string FullRequest;											// The entire input from the client
	string RequestType;											// Type of request (POST, GET etc)
	string FileRequested;										// String folling GET
	  string QueryString;										// Anything after the '?' in the file
	  string Extension;											// Extension of file requested
	  string RealFile;											// Real path to file
	  string RealFileDate;										// Last Modification date of RealFile
	string HTTPVersion;											// HTTP version of the client
	string PostData;											// Data supplied AFTER the double newline, for post requests

	string Headers;												// Headers to be sent with the file
	map <string, bool> Accepts;									// MIME types the client accepts
	string UserAgent;											// Browser used by the user
	string HostRequested;										// Host: from browser
	string From;												// From: value (email address normally)
	string Connection;											// Connection: type (keep alive normally)

	string Date;												// Date/time of this request
	string ModifiedSinceStr;										// IfModifiedSince string
	string UnModifiedSinceStr;										// IfUnModifiedSince string
	bool UseModDate;											// Do we use an If-Modified-Since
	bool UseUnModDate;											// Do we use the If-Unmodified-Since

	int Status;													// Status code for request (404, 200 etc)
	bool UseVH;													// Does the connection use a virtual host or a real one
	bool IsFolder;												// Is the file requested a folder or a file
	bool IsBinary;												// Is the file binary?
	bool IsScript;												// Is the file a script
	bool IsAbsolute;											// Did the client use an absolute address
};

//---------------------------------------------------------------------------------------------
//			Connection::CONNECTION
//---------------------------------------------------------------------------------------------
CONNECTION::CONNECTION(int SFD_SET, struct sockaddr_in CA)
{
	//-----------------------------------------------------------------------------------------
	//			Change settings
	//-----------------------------------------------------------------------------------------
	SFD = SFD_SET;												// Set the socket descriptor
	ClientAddress = CA;											// Assign the client address
	UseVH = false;												// Dont use a Virtual host by default
	IsFolder = false;											// By default its not a folder
	IsBinary = false;											// Nor is it binary
	IsScript = false;											// Or a CGI script
	IsAbsolute = false;											// And the URL is not an absolute URL
	Status = 200;												// But the file is always served fine
}

//---------------------------------------------------------------------------------------------
//			Connection::SetFileType
//---------------------------------------------------------------------------------------------
bool CONNECTION::SetFileType()
{
	// Get the files extension
	int X = RealFile.find_last_of('.') + 1;						// Get the last '.' in the string

	if (X > 0)
	{
		for (X; RealFile[X] != '\0'; X++)						// Copy everything after that to the extension
		{
			Extension+= RealFile[X];						
		}

		if (Options.CGI[Extension].length() > 0)				// If theres an enrty in the CGI map, its a script
		{
			IsScript = true;
			IsBinary = false;
		}
		else if (Options.Binary[Extension])						// Or if theres an entry in the Binary map, its binary
		{
			IsScript = false;
			IsBinary = true;
		}
		else													// It must be text then
		{
			IsBinary = false;
			IsScript = false;
		}
	}
	return 0;
}

//---------------------------------------------------------------------------------------------
//			Connection::ReadRequest
//---------------------------------------------------------------------------------------------
bool CONNECTION::ReadRequest()
{
	//-----------------------------------------------------------------------------------------
	//			Set request variables
	//-----------------------------------------------------------------------------------------
	// First, read in the whole request
	char Buffer[10000];
	recv(SFD, Buffer, 10000, 0);
	
	string Word;												// Temporary place to store each word
	FullRequest = Buffer;

	//-----------------------------------------------------------------------------------------------------
	// Break off any POST data following a double newline
	int Position = FullRequest.find_last_of("\n\n") - 1;
	
	if (Position > 0)	
	{
		FullRequest[Position] = '\0';							// Put a \0 where the \n\n are.

		for ( int C = Position + 2; FullRequest[C] != '\0'; C++)
		{
			PostData += FullRequest[C];							// Copy everything after that to the PostData
		}
	}

	//-----------------------------------------------------------------------------------------------------
	// Split it into words
	istringstream IS(FullRequest);								// Create an istringstream class

	//-----------------------------------------------------------------------------------------------------
	IS >> Word;													// The first word will be the request type
	
	//strupr(Word.c_str());										
	if (!( strcmpi(Word.c_str(), "POST") ||						// Check to see if its a method we support
		 strcmpi(Word.c_str(), "GET")  ||
		 strcmpi(Word.c_str(), "HEAD" )))
	{
		// Its not a request we like
		Status = 501;											// Send a 501 Not Implemented
		return false;
	}
	
	//-----------------------------------------------------------------------------------------------------
	RequestType = Word;											// The request type was ok, assign it
	IS >> FileRequested;										// The next word will be the requested file
	IS >> HTTPVersion;											// Then the HTTP version
	IS >> Word;

	// Before we loop through keys/values, locate the double newline so we know where to stop
	
	//-----------------------------------------------------------------------------------------------------
	// Map keys to values
	while (Word.length())
	{
		// Loop through, mapping keys to values
		if (!strcmpi(Word.c_str(), "From:"))			IS >> From;
		else if(!strcmpi(Word.c_str(), "User-Agent:"))
		{
			IS >> Word;
			while ( !strstr(Word.c_str(), ":") )				// While theres no colons in the word, the word should be added to UserAgent
			{
				UserAgent += Word;
				UserAgent += ' ';
				IS >> Word;
			}
		}
		if(!strcmpi(Word.c_str(), "Host:"))		IS >> HostRequested;
		else if(!strcmpi(Word.c_str(), "Connection:"))	IS >> Connection;
		else if(!strcmpi(Word.c_str(), "If-Modified-Since:"))	// If modified
		{
			IS >> Word;
			UseModDate = true;
			if (Word[3] == ',')
			{
				// Its the first type
				ModifiedSinceStr = Word + ' ';					// We have the day and 5 more words
				IS >> Word;
				ModifiedSinceStr += Word + ' ';
				IS >> Word;
				ModifiedSinceStr += Word + ' ';
				IS >> Word;
				ModifiedSinceStr += Word + ' ';
				IS >> Word;
				ModifiedSinceStr += Word + ' ';
				IS >> Word;
				ModifiedSinceStr += Word + ' ';
			}
			else if (Word.length() > 3)
			{
				// Its the second type
				ModifiedSinceStr = Word + ' ';					// We have the day and 3 more words
				IS >> Word;
				ModifiedSinceStr += Word + ' ';
				IS >> Word;
				ModifiedSinceStr += Word + ' ';
				IS >> Word;
				ModifiedSinceStr += Word + ' ';
			}
			else if (Word[3] != ',')
			{
				// Its the third date type
				ModifiedSinceStr = Word + ' ';					// We have the day and 4 more words
				IS >> Word;
				ModifiedSinceStr += Word + ' ';
				IS >> Word;
				ModifiedSinceStr += Word + ' ';
				IS >> Word;
				ModifiedSinceStr += Word + ' ';
				IS >> Word;
				ModifiedSinceStr += Word + ' ';
			}
		}

		else if(!strcmpi(Word.c_str(), "If-Unmodified-Since:"))	// If Unmodifed	
		{
			IS >> Word;
			UseUnModDate = true;
			if (Word[3] == ',')
			{
				// Its the first type
				UnModifiedSinceStr = Word + ' ';				// We have the day and 5 more words
				IS >> Word;
				UnModifiedSinceStr += Word + ' ';
				IS >> Word;
				UnModifiedSinceStr += Word + ' ';
				IS >> Word;
				UnModifiedSinceStr += Word + ' ';
				IS >> Word;
				UnModifiedSinceStr += Word + ' ';
				IS >> Word;
				UnModifiedSinceStr += Word + ' ';
			}
			else if (Word.length() > 3)
			{
				// Its the second type
				UnModifiedSinceStr = Word + ' ';				// We have the day and 3 more words
				IS >> Word;
				UnModifiedSinceStr += Word + ' ';
				IS >> Word;
				UnModifiedSinceStr += Word + ' ';
				IS >> Word;
				UnModifiedSinceStr += Word + ' ';
			}
			else if (Word[3] != ',')
			{
				// Its the third date type
				UnModifiedSinceStr = Word + ' ';				// We have the day and 4 more words
				IS >> Word;
				UnModifiedSinceStr += Word + ' ';
				IS >> Word;
				UnModifiedSinceStr += Word + ' ';
				IS >> Word;
				UnModifiedSinceStr += Word + ' ';
				IS >> Word;
				UnModifiedSinceStr += Word + ' ';
			}
		}
		// Now, check if its a MIME type. If it DOES NOT have ':' and DOES have '/', then its probably mime
		else if (!strstr(Word.c_str(), ":") && strstr(Word.c_str(), "/"))
		{
			if (strstr(Word.c_str() , ","))
			{
				// There is a comma at the end, get rid of it
				int Y = Word.length() - 1;
				Word[Y] = '\0';
			}
			Accepts[Word] = true;
		}
		IS >> Word;
	}
	
	//-----------------------------------------------------------------------------------------------------
	// First, if the request is HTTP/1.1, there must be a host field
	if ( !strcmpi (HTTPVersion.c_str(), "HTTP/1.1") )
	{
		if (HostRequested.length() <= 0)
		{
			Status = 400;										// No host was specified. Send 400 Bad Request
			return false;
		}
	}
	
	//-----------------------------------------------------------------------------------------------------
	// Cut off absolute URL, making us able to serve all future HTTP versions.
	if (strstr( FileRequested.c_str() , "http://" ))			// If its an absolute URL
	{	 
		IsAbsolute = true;										// Start at the end of the http://
		int CurrLetter = FileRequested.find_first_of("http://") + 7;
		
		for (CurrLetter; FileRequested[CurrLetter] != '/'; CurrLetter++ )
		{
			HostRequested += FileRequested[CurrLetter];			// Copy to the new host.
		}
	} 
	
	//-----------------------------------------------------------------------------------------------------
	// Figue out the virtual host
	ThisHost = &VHI.Host[HostRequested];						
	if(ThisHost->Root.length() < 1)								// If there is an entry for the host in the VHI 
		UseVH = false;											//  then it is a virtual host
	else UseVH = true;

	//-----------------------------------------------------------------------------------------------------
	// Cut off query string
	int X = FileRequested.find_last_of('?') + 1;				// Get the last '?' in the string
	if (X > 0)
	{
		int Y = X;												// Save position of X
		for (X; FileRequested[X] != '\0'; X++)					// Copy everything after that to the extension
		{
			QueryString+= FileRequested[X];						
		}
		FileRequested[Y - 1] = '\0';							// Chop it off at the '?'
	}
		
	//-----------------------------------------------------------------------------------------------------
	// Change slashes from *nix to windows
	for (int Z = 0; FileRequested[Z] != '\0'; Z++)				// Replace / with \ 
	{
		if (FileRequested[Z] == '/') FileRequested[Z] = '\\';
	}
	
	//-----------------------------------------------------------------------------------------------------
	// Assign full path based on virtualhosts
	if (UseVH) RealFile = ThisHost->Root + FileRequested;
	else RealFile = Options.WebRoot + FileRequested;
		
	// Check for a "../", if found send a 404. Because this will allow them to go one folder back, and 
	//  then get files from there, effectivley giving full access to the system
	if (strstr(RealFile.c_str() , "..\\"))
	{
		Status = 404;
		return false;
	}

	//-----------------------------------------------------------------------------------------------------
	// Check if the file is a folder
	DWORD hFile = GetFileAttributes(RealFile.c_str());
	if (hFile == FILE_INVALID)
	{
		
		Status = 404;											// File does not exist. Return error 404
		return false;
	}
	
	//-----------------------------------------------------------------------------------------------------
	if (hFile & FILE_ATTRIBUTE_DIRECTORY)				
	{
		IsFolder = true;										// Is a folder
	}
	else	
	{								
		IsFolder = false;										// Is not folder
	}

	//-----------------------------------------------------------------------------------------------------	
	SetFileType();												// Set whether the file is binary or a script
	Status = 200;												// It passed all the tests, therefore its ok
	return true;
}

//---------------------------------------------------------------------------------------------
//			Connection::HandleRequest
//---------------------------------------------------------------------------------------------
bool CONNECTION::HandleRequest()
{
	// Get the time:
	time_t now;
	struct tm *tm_now;
	char buff[BUFSIZ];

	now = time ( NULL );
	tm_now = gmtime(&now);
	strftime ( buff, sizeof buff, "%a, %d %b %Y %H:%M:%S GMT", tm_now );
	Date = buff;

	//----------------------------------------------------------
	// Do the request
	if (Status == 200)
	{
		if (UseModDate == true)									// If we got a last modified header:
		{
			if (!ModifiedSince(ModifiedSinceStr))				// If the file has not been modifed
			{
				Status = 304;									// Send them a 304 not modifed
			}
		}
		if (UseUnModDate == true)								// If we got a last modified header:
		{
			if (!UnModifiedSince(UnModifiedSinceStr))			// If the file has not been modifed
			{
				Status = 402;									// Send them a 402 not modifed
			}
		}
		// Output the file
		if (IsFolder)											// Request was a folder
		{
			// Search for an index file
			int X = 0;
			bool Found = false;
			while ((X < Options.IndexFiles.size()) && !Found)// While theres still index files there
			{
				string File = RealFile;
				File += "\\";
				File += Options.IndexFiles[X];
				ifstream hFile (File.c_str());
				if (!hFile)
					X++;										// Increase the counter
				else
					Found = true;								// Found one!
			}
			if (Found)
			{
				RealFile += "\\";
				RealFile += Options.IndexFiles[X];				// Make the file were looking for the appropriate index file
				IsFolder = false;									
			}													// Not a folder anymore, break out and send as a file
			// If we are allowed to index:
			if (Options.AllowIndex == true && IsFolder)			// Ensure its still a folder
			{
				bool hResult = IndexFolder();					// Try to index the folder
				if (hResult == false)							// The folder could not be indexed. Report
				{
					Status = 404;								// Set status code
				}
			}
		}
		
		if (!IsFolder)											// Request was a file
		{
			if (IsBinary == true && IsScript == false)
			{
				// The file is a binary file
				Headers = HTTPVersion;							// Send HTTP version
				Headers += " ";											
				Headers += IntToString(Status);					// Send status code
				Headers += " ";			
				Headers += "OK\n";								// Send OK msg
				Headers += "Server: ";
				Headers += Options.Servername;
				Headers += "\nConnection: ";					// Connection type
				Headers += "close\n";
				Headers += "Date: ";
				Headers += Date;
				Headers += "\nContent-type: ";					// Content type
				if (Options.MIMETypes[Extension].length() > 0)	// If we know the mime type
				{
					Headers += Options.MIMETypes[Extension];	// Send it
				}
				else											// Or if we don't know it
				{
					Headers += "image/jpeg";					// Send image/jpg
				}
				Headers += "\nContent-length: ";
				Headers += CalculateSize();
				Headers += "\n\n";								// Double newlines

				send (SFD, Headers.c_str(), Headers.length(), 0);// Send headers

				// Then, if its a GET of POST request, send the file requested
				if ( !strcmpi(RequestType.c_str(), "GET") || !strcmpi(RequestType.c_str(), "POST") )
					SendBinary();
			}
			else if (IsScript == true && IsBinary == false)
			{
				// The file is a CGI script
				Headers = HTTPVersion;							// Send HTTP version
				Headers += " ";											
				Headers += IntToString(Status);					// Send status code
				Headers += " ";			
				Headers += "OK\n";								// Send OK msg
				Headers += "Server: ";
				Headers += Options.Servername;
				Headers += "Date: ";
				Headers += Date;
				Headers += "\nConnection: close ";				// Connection type
				// Note: We do not send the content type OR the double newlines, the
				//  CGI interpreter must do that itself.	

				send (SFD, Headers.c_str(), Headers.length(), 0);		// Send headers

				// Then, if its a GET of POST request, send the file requested
				if ( !strcmpi(RequestType.c_str(), "GET") || !strcmpi(RequestType.c_str(), "POST") )
					SendCGI();
			}
			else
			{
				// The file is plain text
				Headers = HTTPVersion;							// Send HTTP version
				Headers += " ";											
				Headers += IntToString(Status);					// Send status code
				Headers += " ";			
				Headers += "OK\n";								// Send OK msg
				Headers += "Server: ";
				Headers += Options.Servername;
				Headers += "\nConnection: ";					// Connection type
				Headers += "close\n";
				Headers += "Date: ";
				Headers += Date;
				Headers += "\nContent-type: ";					// Content type
				if (Options.MIMETypes[Extension].length() > 0)	// If we know the mime type
				{
					Headers += Options.MIMETypes[Extension];	// Send it
				}
				else											// Or if we don't know it
				{
					Headers += "text/plain";					// Send text/plain
				}
				Headers += "\n\n";								// Double newlines
				send (SFD, Headers.c_str(), Headers.length(), 0);// Send headers
	
				// Then, if its a GET of POST request, send the file requested
				if ( !strcmpi(RequestType.c_str(), "GET") || !strcmpi(RequestType.c_str(), "POST"))
					SendText();
			}
		}
	}
	// If the status wasn't 200 to start with, or it was somehow changed along the way:
	if (Status != 200)
	{
		SendError();											// Send the error
		return false;
	}
	return true;												// No errors. Return true
}

//---------------------------------------------------------------------------------------------
//			Connection::SendText
//---------------------------------------------------------------------------------------------
bool CONNECTION::SendText()
{
	char Buffer[10000];											// Buffer to store data
	string Text;												// String to store everything
	ifstream hFile (RealFile.c_str());		
	while (!hFile.eof())										// Go until the end
	{
		hFile.getline(Buffer, 10000);							// Read a full line
		Text += Buffer;											// Put it in the string
		Text += "\n";											// Remember to add the \n
	}
	hFile.close();												// Close
																// Send the data
	int Y = send (SFD, Text.c_str(), Text.length(), 0);
	if (Y != 0)					
		return true;											// It sent fine
	else
		return false;											// Errors sending
}

//---------------------------------------------------------------------------------------------
//			Connection::SendBinary
//---------------------------------------------------------------------------------------------
bool CONNECTION::SendBinary()
{
	// WOOHOO! Do not lose this function, it took me ages to learn how to send binary files,
	//  and now it finally works.
	char Buffer[10000];
																// Open the file as binary
	ifstream hFile (RealFile.c_str(), ios::binary);
	int X = 0;
	while (!hFile.eof())                                        // Keep reading it in
    {
        hFile.read(Buffer, 10000);
        int Y = send(SFD, Buffer, hFile.gcount(), 0);			// Send data as we read it
    }
    hFile.close();                                              // Close  
    return true;
}

//---------------------------------------------------------------------------------------------
//			Connection::SendCGI
//---------------------------------------------------------------------------------------------
bool CONNECTION::SendCGI()
{
	if (CGICounter > 1000)										// Check the counter hasn't gotten too high
	{
		CGICounter = 0;
	}

	string OutFile = "C:\\SWS\\CGI\\Out\\out";					// File to output to
	OutFile += IntToString(CGICounter);
	OutFile += ".txt";

	string Command = Options.CGI[Extension];					// Create the command
	Command += " ";
	Command += RealFile;										// File to interpret
	Command += "?";
	Command += QueryString;
	Command += " > ";
	Command += OutFile;											// Pipe to output file				

	LogText(Command);

	// Set environment variables
	
	string SERVER_SOFTWARE = "SERVER_SOFTWARE=";
		SERVER_SOFTWARE += Options.Servername;
	string SERVER_INTERFACE = "SERVER_INTERFACE=CGI/1.1";
	string REDIRECT_STATUS = "REDIRECT_STATUS=200";

	putenv(SERVER_SOFTWARE.c_str());
	putenv(SERVER_INTERFACE.c_str());
	putenv(REDIRECT_STATUS.c_str());

	
	string FileRequestedWebStyle = FileRequested;				// Set the request back to web style

	for (int Z = 0; FileRequestedWebStyle[Z] != '\0'; Z++)		// Replace \ with / 
	{
		if (FileRequestedWebStyle[Z] == '\\') FileRequestedWebStyle[Z] = '/';
	}


	string SERVER_PROTOCOL = "SERVER_PROTOCOL=";				// Create all the env. variables
		SERVER_PROTOCOL += HTTPVersion;
	string SERVER_PORT = "SERVER_PORT=";
		SERVER_PORT += IntToString(Options.Port);
	string REQUEST_METHOD = "REQUEST_METHOD=";
		REQUEST_METHOD += RequestType;
	string PATH_INFO = "PATH_INFO=";
		PATH_INFO += FileRequestedWebStyle;
	string PATH_TRANSLATED = "PATH_TRANSLATED=";
		PATH_TRANSLATED += RealFile;
	// SCRIPT_NAME 
	string QUERY_STRING = "QUERY_STRING=\"?";
		QUERY_STRING += QueryString;
		QUERY_STRING += "\"";
	string REMOTE_ADDR = "REMOTE_ADDR=";
		REMOTE_ADDR += inet_ntoa(ClientAddress.sin_addr);

	putenv(SERVER_PROTOCOL.c_str());							// Set all the env. variables
	putenv(SERVER_PORT.c_str());
	putenv(REQUEST_METHOD.c_str());
	putenv(PATH_INFO.c_str());
	putenv(PATH_TRANSLATED.c_str());
	putenv(QUERY_STRING.c_str());
	putenv(REMOTE_ADDR.c_str());
	LogText(QUERY_STRING);

	system(Command.c_str());									// Do the command


	char Buffer[10000];											// Buffer to store data
	string Text;												// String to store everything
	ifstream hFile (OutFile.c_str());		
	while (!hFile.eof())										// Go until the end
	{
		hFile.getline(Buffer, 10000);							// Read a full line
		Text += Buffer;											// Put it in the string
		Text += "\n";											// Remember to add the \n
	}
	hFile.close();	

	CGICounter++;												// Increase the counter

	DeleteFile(OutFile.c_str());								// Delete the file (clean up after ourselves)

	int Y = send (SFD, Text.c_str(), Text.length(), 0);
	if (Y != 0)					
		return true;											// It sent fine
	else
		return false;											// Errors sending
	return 0;
}

//---------------------------------------------------------------------------------------------
//			Connection::IndexFolder()
//---------------------------------------------------------------------------------------------
bool CONNECTION::IndexFolder()
{
	// Check if we are allowed
	if (Options.AllowIndex == false)
	{
		Status = 404;
		return false;
	}
	else
	{	
		// Open and list folder contents
		HANDLE hFind;
		WIN32_FIND_DATA FindData;
		int ErrorCode;
		BOOL Continue = true;

		string FileIndex = RealFile + "\\*.*";					// List all files
		hFind = FindFirstFile(FileIndex.c_str(), &FindData);

		string Text;
		for (int Z = 0; FileRequested[Z] != '\0'; Z++)			// Replace \ with / 
		{
			if (FileRequested[Z] == '\\') FileRequested[Z] = '/';
		}

		//-----------------------------
		if(hFind == INVALID_HANDLE_VALUE)
		{
			Status = 404;
			return false;
		}
		else
		{	
			Headers = HTTPVersion;								// Send HTTP version
			Headers += " 200 OK\n";								
			Headers += "Server: SWS Stovell Web Server 2.0\n";	// Server name
			Headers += "Connection: close";
			Headers += "\nContent-type: text/html";				// Content type
			Headers += "\n\n";									// Double newlines
			send (SFD, Headers.c_str(), Headers.length(), 0);	// Send headers

			// Most of this is all HTML bieng generated													
			Text = "<html>\n<head>\
\n<title>Index of ";
			Text += FileRequested;								// Insert the file requested					
			Text += "</title>\
\n</head>\
\
\n<body>\
\n<!-- Title -->\
\n<p align=\"center\">\
\n  <font face=\"Verdana\" size=\"6\">\
\n    Index of ";
			Text += FileRequested;								// Insert the file requested
			Text += "\n  </font>\
\n</p>\
\n\
\n<hr>\
\n\
\n<center>\
\n<table border=\"0\" width=\"75%\" height=\"6\" cellspacing=\"0\" cellpadding=\"4\">\
\n  <tr>\
\n    <td width=\"40%\" height=\"1\" bgcolor=\"#0080C0\"><p align=\"center\"><strong><small><font\
\n    face=\"Verdana\" color=\"#FFFFFF\">Name</font></small></strong></td>\
\n    <td width=\"23%\" height=\"1\" bgcolor=\"#0080C0\"><p align=\"center\"><font face=\"Verdana\"\
\n    color=\"#FFFFFF\"><strong><small>Size</small></strong></font></td>\
\n    \
\n  </tr>";
																// Now, print the first entry
			Text += "  <tr>\
\n    <td width=\"40%\" height=\"0\" bgcolor=\"#E2E2E2\"><p align=\"left\"><small><font face=\"Verdana\">";
			Text += "<a href=\"";
			Text += FileRequested;								// Current folder
			Text += "/";
			Text += FindData.cFileName;							// Name of file
			Text += "\">";
			Text += FindData.cFileName;							// Name of file
			Text += "</a></font></small></td>\n";
			Text += "\n    <td width='23%' height='0' bgcolor='#C0C0C0'>\
\n    <p align='center'><small><font face='Verdana'>";
			if (IntToString(FindData.nFileSizeHigh)[0] != '0')
				Text += IntToString(FindData.nFileSizeHigh);
			if (IntToString(FindData.nFileSizeLow)[0] != '0')
				Text += IntToString(FindData.nFileSizeLow);
			Text += "\n</font></small></td>\
\n    </font></small></td>";
			send(SFD, Text.c_str(), Text.length(), 0);
		}

		if (Continue)
		{
			while (FindNextFile(hFind, &FindData))
			{
				Text = "  <tr>\
\n    <td width=\"40%\" height=\"0\" bgcolor=\"#E2E2E2\"><p align=\"left\"><small><font face=\"Verdana\">";
			Text += "<a href=\"";
			if (strcmpi(FileRequested.c_str(), "/"))
				Text += FileRequested;								// Current folder
			Text += "/";
			Text += FindData.cFileName;							// Name of file
			Text += "\">";
			Text += FindData.cFileName;							// Name of file
			Text += "</a></font></small></td>\n";
			Text += "\n    <td width='23%' height='0' bgcolor='#C0C0C0'>\
\n    <p align='center'><small><font face='Verdana'>";
			
			if (IntToString(FindData.nFileSizeHigh)[0] != '0')
				Text += IntToString(FindData.nFileSizeHigh);
			if (IntToString(FindData.nFileSizeLow)[0] != '0')
				Text += IntToString(FindData.nFileSizeLow);
			Text += "\n</font></small></td>\
\n    </font></small></td>";
				send(SFD, Text.c_str(), Text.length(), 0);
			}

			ErrorCode = GetLastError();

			if (ErrorCode == ERROR_NO_MORE_FILES)
			{
				Text = "</tr>\
\n</table>\
\n</div>\
\n\
\n<hr>\
\n\
\n<p align='center'><small><small><font face='Verdana'>Index produced automatically by <a\
\nhref='http://swebs.sourceforge.net'>SWS Web Server</a></font></small></small></p>\
\n</body>\
\n</html>";
				send(SFD, Text.c_str(), Text.length(), 0);
			}

        FindClose(hFind);
       
		}
	return true;
	}
}

//---------------------------------------------------------------------------------------------
//			Connection::SendError()
//			Sends the appropriate error.
//---------------------------------------------------------------------------------------------
bool CONNECTION::SendError()
{	
	RealFile = Options.ErrorDirectory;							// Where the error files are kept
	RealFile += '\\';											// Add a backslash to be safe
	RealFile += IntToString(Status);							// Error code
	RealFile += ".html";										// Extension

	char Buffer[10000];											// Buffer to store data

	string ErrorPage;

	ifstream hFile (RealFile.c_str());	
	if (hFile)													// The file exists
	{
		ErrorPage = "HTTP/1.1 ";								// Since we have an HTML file to send them, just send a 200
		ErrorPage += "200";
		ErrorPage += ' ';
		ErrorPage += "OK";
		ErrorPage += "\nContent-type: text/html\n\n";
		while (!hFile.eof())									// Go until the end
		{
			hFile.getline(Buffer, 10000);						// Read a full line
			ErrorPage += Buffer;								// Put it in the string
			ErrorPage += "\n";									// Remember to add the \n
		}
		hFile.close();											// Close
	}
	
	else														// There was no custom error page for this code
	{					
		ErrorPage = "HTTP/1.1 ";								
		ErrorPage += IntToString(Status);						// Send the appropriate status code
		ErrorPage += ' ';
		ErrorPage += Options.ErrorCode[Status];
		ErrorPage += "\nContent-type: text/html\n\n";

		ErrorPage += "<html><body><center><b>";					// Send a basic and boring looking error page
		ErrorPage += IntToString(Status);
		ErrorPage += " ";
		ErrorPage += Options.ErrorCode[Status];
		ErrorPage += "</b></body></html>";
	}
	int Y = send (SFD, ErrorPage.c_str(), ErrorPage.length(), 0);

	if (Y != 0)					
		return true;											// It sent fine
	else
		return false;											// Errors sending
	
	return false;
}

//---------------------------------------------------------------------------------------------
//			Connection::LogText()
//			Logs the string passed as an argument. This is just a temporary log, 
//			used for testing. A propper logging function must be written to replace this.
//---------------------------------------------------------------------------------------------
bool CONNECTION::LogText(string Text)
{
	FILE* log;
	log = fopen("C:\\SWS\\testlog.txt", "a+");
	if (log == NULL)
      return false;
	fprintf(log, "%s", Text.c_str());
	fclose(log);
	return true;
}

//---------------------------------------------------------------------------------------------
//			Connection::CalculateSize()
//			Calculates and returns the size of the file requested.
//---------------------------------------------------------------------------------------------
string CONNECTION::CalculateSize()
{
	string ReturnValue;
	unsigned long Size = 0;

	HANDLE hFile = CreateFile(
				RealFile.c_str(), 
				GENERIC_READ,									// open for reading 
                FILE_SHARE_READ,								// share for reading 
                NULL,											// no security 
                OPEN_EXISTING,									// existing file only 
                FILE_ATTRIBUTE_NORMAL,							// normal file 
                NULL);											// no attr. template 

	if (hFile != INVALID_HANDLE_VALUE)							// If the file was opened
	{
		Size = GetFileSize(hFile, NULL);						// Get the size
	}
	
	ReturnValue = IntToString(Size);							// Convert it to a printable string
	return ReturnValue;
}


//---------------------------------------------------------------------------------------------
//			Connection::ModifiedSince()
//			Checks if the file was modified since the given date
//---------------------------------------------------------------------------------------------
bool CONNECTION::ModifiedSince(string Date)
{
	//----------------------------------------------------------
	string Temp;
	time_t now;
	struct tm *tm_now = new tm;
	struct tm *tm_date = new tm;

	// Get the time the file was last modified:
	SYSTEMTIME myTime;											// Windows time structure
	HANDLE hFind;
    WIN32_FIND_DATA FindData;

	now = time ( NULL );										// Get the current time
	tm_date = localtime ( &now );
	
	hFind = FindFirstFile(RealFile.c_str(), &FindData);			// Open the file
	FileTimeToSystemTime(&FindData.ftLastWriteTime, &myTime);	// Get its time
	
	tm_now->tm_hour = myTime.wHour + 1;							// Copy the time settings
	tm_now->tm_min = myTime.wMinute;
	tm_now->tm_mday = myTime.wDay;
	tm_now->tm_mon = myTime.wMonth - 1;
	tm_now->tm_sec = myTime.wSecond;
	tm_now->tm_year = myTime.wYear;
	tm_now->tm_year -= 1900;

	//----------------------------------------------------------
	int Day;
	string Month;
	int Year;
	int Hour;
	int Minute;
	int Second;
	
	int Type = 0;

	istringstream DateString1(Date);
	DateString1 >> Temp;

	if (Temp[3] == ',')
	{
		Type = 1;
		//------------------------------------------------------
		for (int X = 0; X < Date.length(); X++)
		{
			if ( Date[X] == ':' )
				Date[X] = ' ';
		}

		istringstream DateString(Date);
		DateString >> Temp;
		//------------------------------------------------------
		DateString >> Day;
		DateString >> Month;
		DateString >> Year;
		DateString >> Hour;
		DateString >> Minute;
		DateString >> Second;
	}
	else if (Temp.length() > 3)
	{
		Type = 2;
		//------------------------------------------------------
		for (int X = 0; X < Date.length(); X++)
		{
			if ( Date[X] == ':' )
				Date[X] = ' ';
		}
		for (int Z = 0; Z < Date.length(); Z++)
		{
			if ( Date[Z] == '-' )
				Date[Z] = ' ';
		}
		istringstream DateString(Date);
		DateString >> Temp;
		//------------------------------------------------------
		DateString >> Day;
		DateString >> Month;
		DateString >> Year;
		Year += 2000;
		if (Year > 2050)
			return true;
		DateString >> Hour;
		DateString >> Minute;
		DateString >> Second;
	}
	else if (Temp[3] != ',')
	{
		Type = 3;
		//------------------------------------------------------
		for (int X = 0; X < Date.length(); X++)
		{
			if ( Date[X] == ':' )
				Date[X] = ' ';
		}

		istringstream DateString(Date);
		DateString >> Temp;
		//------------------------------------------------------
		
		DateString >> Month;
		DateString >> Day;
		DateString >> Hour;
		DateString >> Minute;
		DateString >> Second;
		DateString >> Year;
	}

	//----------------------------------------------------------
	tm_date->tm_hour = Hour;
	tm_date->tm_mday = Day;
	tm_date->tm_min = Minute;
	tm_date->tm_mon = CalcMonth(Month);
	tm_date->tm_sec = Second;
	tm_date->tm_year = Year - 1900;
	
	//----------------------------------------------------------
	
	int Y = difftime(mktime(tm_date), mktime(tm_now));
	delete tm_now;
	delete tm_date;
	if (Y < 0)
	{
		return true;
	}
	else return false;
}


//---------------------------------------------------------------------------------------------
//			Connection::UnModifiedSince()
//			Returns a negative of ModifiedSince()
//---------------------------------------------------------------------------------------------
bool CONNECTION::UnModifiedSince(string Date)
{
	bool X = ModifiedSince(Date);
	if (X)
	{
		return false;
	}
	else 
	{	
		return true;
	}
}
//---------------------------------------------------------------------------------------------
#endif
