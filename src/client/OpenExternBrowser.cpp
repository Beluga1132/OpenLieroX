/*
 *  OpenExternBrowser.cpp
 *  OpenLieroX
 *
 *  Created by Albert Zeyer on 29.09.08.
 *  code under LGPL
 *
 */


#include <string>
#include <list>
#include <cstdlib>

#if !defined(__APPLE__) && !defined(WIN32)
#include <unistd.h>
#include <sys/wait.h>
#endif

#ifdef __APPLE__
#include <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
// This confused Boost. Comes from <AssertMacros.h>
#undef check
#undef __Check
#endif

#ifdef WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "Debug.h"
#include "LieroX.h"
#include "StringUtils.h"


// Links come from chat and HTML sent by other players and servers,
// so only open plain web URLs.
static bool IsSafeWebUrl(const std::string& url) {
	const std::string lower = stringtolower(url);
	if (lower.compare(0, 7, "http://") != 0 && lower.compare(0, 8, "https://") != 0)
		return false;
	for (size_t i = 0; i < url.size(); ++i)
		if ((unsigned char)url[i] <= ' ' || url[i] == 127)
			return false;
	return true;
}

#if !defined(__APPLE__) && !defined(WIN32)
// Run the first browser that exists with the URL as its only argument.
// No shell is involved, so the URL can't inject commands.
// We double-fork so the browser is reparented and we don't leave zombies.
static void LaunchBrowser(const std::list<std::string>& browsers, const std::string& url) {
	pid_t child = fork();
	if (child < 0) {
		warnings << "cannot fork to start a browser" << endl;
		return;
	}
	if (child == 0) {
		if (fork() != 0)
			_exit(0);
		for (std::list<std::string>::const_iterator it = browsers.begin(); it != browsers.end(); ++it)
			execlp(it->c_str(), it->c_str(), url.c_str(), (char*)NULL);
		_exit(127);
	}
	waitpid(child, NULL, 0);
}
#endif

void OpenLinkInExternBrowser(const std::string& url) {
	notes << "open in extern browser: " << url << endl;
	if (!IsSafeWebUrl(url)) {
		warnings << "not opening link, only http and https URLs are allowed: " << url << endl;
		return;
	}

#if defined(__APPLE__)
	// Thanks to Jooleem project (http://jooleem.sourceforge.net) for the code

	// Create a string ref of the URL:
	CFStringRef cfurlStr = CFStringCreateWithCString( NULL, url.c_str(), kCFStringEncodingASCII);

	// Create a URL object:
	CFURLRef cfurl = CFURLCreateWithString (NULL, cfurlStr, NULL);

	// Open the URL:
	LSOpenCFURLRef(cfurl, NULL);

	// Release the created resources:
	CFRelease(cfurl);
	CFRelease(cfurlStr);

#elif defined(WIN32)
	ShellExecute(NULL, "open", url.c_str(), NULL, NULL, SW_MAXIMIZE);
	
#else
	std::list<std::string> browsers;
	if (getenv("BROWSER") != NULL) {
		std::string tmp = getenv("BROWSER");
		if(tmp != "") browsers.push_back(tmp);
	}
	browsers.push_back("xdg-open"); // part of XdgUtils from FreeDesktop.org
	browsers.push_back("gnome-open"); // available on most Gnome systems
	browsers.push_back("sensible-browser"); // Ubuntu and others seem to provide this
	browsers.push_back("firefox");
	browsers.push_back("mozilla-firefox");
	browsers.push_back("konqueror");
	browsers.push_back("mozilla");
	browsers.push_back("opera");
	browsers.push_back("epiphany");
	browsers.push_back("galeon");
	browsers.push_back("netscape");

	LaunchBrowser(browsers, url);
#endif
}

