#ifndef OPTIONSHPP
#define OPTIONSHPP 1
//----------------------------------------------------------------------------------------------------
#include <windows.h>
#include <map>
#include <sstream>

// These files are to be used for parsing XML documents (the config file) and must be downloaded
//  from: www.xml-parser.com
#include <CkXml.h>
#include <CkString.h>
#include <CkSettings.h>

using namespace std;

// The first two of these files come from the Chilkat XML library, freely downloadable from
//  www.xml-parser.com
#pragma comment(lib, "ChilkatRelSt.lib")
#pragma comment(lib, "CkBaseRelSt.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "crypt32.lib")

#pragma warning(disable:4786)

int StringToInt(string);
//----------------------------------------------------------------------------------------------------
//			Options class - derived from configuration file
//----------------------------------------------------------------------------------------------------
class OPTIONS
{
  public:
	string Servername;											// Name of this server - Ie, Central Online (SWS)
	int Port;													// Port number to listen on (80)
	string WebRoot;												// Path to root web folder (C:\WebRoot)
	int MaxConnections;											// Number of connections at once (20)
	string Logfile;												// Path/name of log file (c:\SWS\logfile.log)
	map <string, string> CGI;									// Map of extension/interpreter for CGI scripts (ie, CGI["php"] = "C:\PHP.exe"
	map <int, string> IndexFiles;								// Files that will be used as auto indexes of folders (index.htm)
	int Timeout;												// Idle time for each connection before time out and closure
	map <string, string> MIMETypes;								// MIME types
	map <string, bool> Binary;									// Files that should be opened as binary
	bool AllowIndex;											// Are we allowed to index files
	map <int, string> ErrorCode;								// List of number to string mapped error codes, ie:
																//  ErrorCode[404] = "File Not Found";
	string ErrorDirectory;										// Folder where custom error pages are kept
	bool ReadSettings();										// Read in the settings from the config file
}Options;



//----------------------------------------------------------------------------------------------------
//			Virtual Host class - derived from configuration file
//----------------------------------------------------------------------------------------------------
class VIRTUALHOST
{
public:
	string Name;												// Name of this virtual host (ie, RateMyPoo)
	string HostName;											// Internet Address of host (www.ratemypoo.com)
	map <int, string> IndexFiles;								// Files that will be used as auto indexes of folders (index.htm)
	string Root;												// Root folder of files for this VH (ie, c:\RateMyPoo)
	string Logfile;												// Path/name of log file (C:\RateMyPoo\logfile.log)
};

//----------------------------------------------------------------------------------------------------
//			Virtual Host Index (VHI) class. Keeps an index of all the Virtual hosts
//----------------------------------------------------------------------------------------------------
class VirtualHostIndex
{
  public:
	int NumberOfHosts;
	map <string, VIRTUALHOST> Host;								// Map Internet address to appropriate virtual host
}VHI;

//----------------------------------------------------------------------------------------------------
//			IntToString();
//----------------------------------------------------------------------------------------------------
string IntToString(int num)
{
  ostringstream myStream; 										//creates an ostringstream object
  myStream << num << flush;
  return(myStream.str()); 										//returns the string form of the stringstream object
}

//----------------------------------------------------------------------------------------------------
//			StringToInt();
//----------------------------------------------------------------------------------------------------
int StringToInt(string str)
{
   std::istringstream is(str);
   int i;    
   
   is >> i;    
   return i;
}



//----------------------------------------------------------------------------------------------------
//			Options::ReadSettings()
//----------------------------------------------------------------------------------------------------
bool OPTIONS::ReadSettings()
{
	// Locate the configuration file - its location is inthe registry as
	// HKEY_LOCAL_SYSTEM\\Software\\SWS\\ConfigFile

	HKEY hKey;													// Handle for the key
	unsigned long dwDisp;										// Disposition
	RegCreateKeyEx(HKEY_LOCAL_MACHINE, TEXT("Software\\SWS"), 0,
               NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, &dwDisp);

	unsigned char Buffer[_MAX_PATH];
	unsigned long DataType;
	unsigned long BufferLength = sizeof(Buffer);
	
	RegQueryValueEx(hKey, "ConfigFile", NULL, &DataType, Buffer, &BufferLength);

	string ConfigFileLocation;
	ConfigFileLocation = (char *)Buffer;						// Copy the config file location

	if ( ConfigFileLocation.empty())							// If the key was not there
	{
		return 0;
	}

	RegCloseKey(hKey);
	
	//===========================
	//	Open XML file
	//===========================
	CkXml xml;
    xml.LoadXmlFile(ConfigFileLocation.c_str());				// Use the file we just got from the registry 
	
	// Server's name
	CkXml *node = xml.SearchForTag(0,"ServerName");				// Find the server name first
	CkXml *node2;												// Used later

	if (node)
	{
		Servername = node->get_Content();
	}
	// Port number
	node = xml.SearchForTag(0,"Port");							// Find the port
	if (node)
	{									
		string SPort = node->get_Content();						// Put it in a string
		Port = StringToInt(SPort);								// Convert to integer
	}

	// Webroot
	node = xml.SearchForTag(0,"Webroot");						// Root web folder
	if (node)
	{									
		WebRoot = node->get_Content();	
	}
	// Max connections
	node = xml.SearchForTag(0,"MaxConnections");				// Max connections
	if (node)
	{
		MaxConnections = StringToInt(node->get_Content());
	}
	
	// Log file
	node = xml.SearchForTag(0,"LogFile");
	if (node)
	{
		Logfile = node->get_Content();
	}
	
	// ErrorPages
	node = xml.SearchForTag(0,"ErrorPages");
	if (node)
	{
		Options.ErrorDirectory = node->get_Content();
	}

	// Loop through with the CGI entries
	node = xml.SearchForTag(0,"CGI");
	while (node)
	{
		// Map Extension to Interpreter
		node2 = xml.SearchForTag(node, "Extension");
		string cExt;
		if (node2)
		{
			cExt = node2->get_Content();
		}
		else
		{
			node2 = xml.SearchForTag(node, "Interpreter");
			if (node2)
			{
				CGI[cExt] = node2->get_Content();
			}
		}

		// Search for the next ArticleTitle tagged node beginning with the node
		// just after the current node in a breadth-first document tree traversal.
		CkXml *curNode = node;

		node = xml.SearchForTag(curNode,"CGI");
		delete curNode;
	}
	
	// Index files
	node = xml.SearchForTag(0,"IndexFile");
	int X = 0;
	while (node)
	{
		IndexFiles[X] = node->get_Content();
		X++;
		
		CkXml *curNode = node;
		node = xml.SearchForTag(curNode,"IndexFile");
		delete curNode;
	}
	
	// Allow indexing
	node = xml.SearchForTag(0,"AllowIndex");
	if (node)
	{
		if ( !strcmpi(node->get_Content(), "true" ))
		{
			AllowIndex = true;
		}
		else AllowIndex = false;
	}

	// Virtual Hosts
	string sName;
	string sHostName;
	string sRoot;
	string sLogFile;
	string Index;
	node = xml.SearchForTag(0,"VirtualHost");
	while (node)
	{
		node2 = xml.SearchForTag(node, "vhName");
		if (node2)
		{
			sName = node2->get_Content();
		}
		
		node2 = xml.SearchForTag(node, "vhHostName");
		if (node2)
		{
			sHostName = node2->get_Content();
		}

		node2 = xml.SearchForTag(node, "vhRoot");
		if (node2)
		{
			sRoot = node2->get_Content();
		}

		node2 = xml.SearchForTag(node, "vhLogFile");
		if (node2)
		{
			sLogFile = node2->get_Content();
		}

		if ( !sName.empty() && !sHostName.empty() && !sRoot.empty() && !sLogFile.empty())
		{
			VHI.Host[sHostName].HostName = sHostName;
			VHI.Host[sHostName].Logfile = sLogFile;
			VHI.Host[sHostName].Name = sName;
			VHI.Host[sHostName].Root = sRoot;
		}

		CkXml *curNode = node;

		node = xml.SearchForTag(curNode,"VirtualHost");
		delete curNode;
	}


	return 1;
}


//----------------------------------------------------------------------------------------------------
//			CalcMonth() - returns the integer description of a month from its string, ie, "Feb" returns 2
//----------------------------------------------------------------------------------------------------
int CalcMonth(string Month)
{
	if ( !strcmpi(Month.c_str(), "Jan")   )
		return 0;
	else if ( !strcmpi(Month.c_str(), "Feb")   )
		return 1;
	else if ( !strcmpi(Month.c_str(), "Mar")   )
		return 2;
	else if ( !strcmpi(Month.c_str(), "Apr")   )
		return 3;
	else if ( !strcmpi(Month.c_str(), "May")   )
		return 4;
	else if ( !strcmpi(Month.c_str(), "Jun")   )
		return 5;
	else if ( !strcmpi(Month.c_str(), "Jul")   )
		return 6;
	else if ( !strcmpi(Month.c_str(), "Aug")   )
		return 7;
	else if ( !strcmpi(Month.c_str(), "Sep")   )
		return 8;
	else if ( !strcmpi(Month.c_str(), "Oct")   )
		return 9;
	else if ( !strcmpi(Month.c_str(), "Nov")   )
		return 10;
	else if ( !strcmpi(Month.c_str(), "Dec")   )
		return 11;
	else return 0;
}
//----------------------------------------------------------------------------------------------------
#endif