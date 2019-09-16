#include <algorithm>
#include <curl/curl.h>
#include <cstring>
#include <fstream>
#include <libxml/xmlreader.h>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

static const vector<string> repos =
{
	"https://repo1.maven.org/maven2",
	"https://dl.google.com/dl/android/maven2",
	"https://maven.fabric.io/public",
};

class Package
{
	static size_t writeBytes(void *ptr, size_t size, size_t nmemb, vector<char>* dst);
	static void getBytes(const string& url, vector<char>& bytes);
	const vector<char>& getPomBytes();
	const vector<char>& getPackageBytes();
	const string& getRepo();

	vector<char> pomBytes;
	vector<char> packageBytes;
	string repo;
	
	bool isJar();
	bool isAar();

public :

	const string groupId;
	const string name;
	const string version;

	const string getFullName() const;
	
	int storePackage(const string& directory);
	void getDependencies(vector<Package>& dependencies);
		
	Package(const string& groupId_, const string& name_, const string& version_) :
		groupId(groupId_), name(name_), version(version_) { }
		
	bool operator==(const Package& other)
	{
		return (groupId == other.groupId) &&
			(name == other.name) && (version == other.version);
	}
};

const string& Package::getRepo()
{
	const vector<char>& pomBytes = getPomBytes();
	return repo;
}

const string Package::getFullName() const
{
	stringstream ss;
	ss << groupId << ':' << name << ':' << version;
	return ss.str();
}

size_t Package::writeBytes(void *ptr, size_t size, size_t nmemb, vector<char>* dst_)
{
	vector<char>& dst = *dst_;
	size_t oldSize = dst.size();
	const size_t written = size * nmemb;
	dst.resize(dst.size() + written);
	memcpy(reinterpret_cast<char*>(&dst[oldSize]), ptr, written);
    return written;
}

void Package::getBytes(const string& url, vector<char>& bytes)
{
	CURL* curl = curl_easy_init();
	if (!curl) return;

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Package::writeBytes);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, reinterpret_cast<void*>(&bytes));
	CURLcode res = curl_easy_perform(curl);
	if (res)
	{
		fprintf(stderr, "-> Error %d in CURL\n", (int)res);
	}
	curl_easy_cleanup(curl);
}

const vector<char>& Package::getPomBytes()
{
	if (pomBytes.size()) return pomBytes;
	
	bool valid = false;
	for (int i = 0; i < repos.size(); i++)
	{
		repo = repos[i];

		stringstream ss;
		ss << repo << '/';
		string path = groupId;
		replace(path.begin(), path.end(), '.', '/');
		ss << path << '/' << name << '/' << version << '/';
		ss << name << '-' << version << ".pom";
		const string& url = ss.str();
		
		getBytes(url, pomBytes);

		xmlTextReaderPtr reader = xmlReaderForMemory(
			reinterpret_cast<const char*>(&pomBytes[0]),
			pomBytes.size(), "", NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);

		if (!reader) continue;

		int ret = xmlTextReaderRead(reader);
		while (ret == 1)
			ret = xmlTextReaderRead(reader);

		xmlFreeTextReader(reader);

		if (ret == 0)
		{
			valid = true;
			break;
		}
		
		pomBytes.clear();
	}
	
	if (!valid)
	{
		fprintf(stderr, "-> Cound not find a valid POM in any repository\n");
	}

	return pomBytes;
}

bool Package::isJar()
{
	const vector<char>& pomBytes = getPomBytes();
	if (!pomBytes.size()) return false;

	xmlTextReaderPtr reader = xmlReaderForMemory(
		reinterpret_cast<const char*>(&pomBytes[0]),
		pomBytes.size(), "", NULL, 0);

	if (!reader) return false;

	// Find <packaging> node and check its value
	for (int ret = xmlTextReaderRead(reader); ret == 1; ret = xmlTextReaderRead(reader))
	{
		xmlNodePtr nodePtr = xmlTextReaderCurrentNode(reader);
		if (!nodePtr) continue;
		if (!nodePtr->name) continue;
		
		if (strcmp((const char*)nodePtr->name, "packaging")) continue;
		
		xmlChar* innerXml = xmlTextReaderReadInnerXml(reader);
		if (!strcmp((const char*)innerXml, "jar"))
		{
			xmlFree(innerXml);
			xmlFreeTextReader(reader);
			return true;
		}

		xmlFree(innerXml);
	}

	xmlFreeTextReader(reader);
	return false;
}

bool Package::isAar()
{
	const vector<char>& pomBytes = getPomBytes();
	if (!pomBytes.size()) return false;

	xmlTextReaderPtr reader = xmlReaderForMemory(
		reinterpret_cast<const char*>(&pomBytes[0]),
		pomBytes.size(), "", NULL, 0);

	if (!reader) return false;

	// Find <packaging> node and check its value
	for (int ret = xmlTextReaderRead(reader); ret == 1; ret = xmlTextReaderRead(reader))
	{
		xmlNodePtr nodePtr = xmlTextReaderCurrentNode(reader);
		if (!nodePtr) continue;
		if (!nodePtr->name) continue;
		
		if (strcmp((const char*)nodePtr->name, "packaging")) continue;
		
		xmlChar* innerXml = xmlTextReaderReadInnerXml(reader);
		if (!strcmp((const char*)innerXml, "aar"))
		{
			xmlFree(innerXml);
			xmlFreeTextReader(reader);
			return true;
		}

		xmlFree(innerXml);
	}

	xmlFreeTextReader(reader);
	return false;
}

const vector<char>& Package::getPackageBytes()
{
	if (packageBytes.size()) return packageBytes;

	stringstream ss;
	ss << getRepo() << '/';
	string path = groupId;
	replace(path.begin(), path.end(), '.', '/');
	ss << path << '/' << name << '/' << version << '/';
	ss << name << '-' << version;
	if (isAar())
		ss << ".aar";
	else
		// ".jar" is the default
		ss << ".jar";
	const string& url = ss.str();
	
	getBytes(url, packageBytes);
	return packageBytes;
}

int Package::storePackage(const string& directory)
{	
	const vector<char>& packageBytes = getPackageBytes();

	const string fullname = getFullName();
	if (!packageBytes.size())
	{
		fprintf(stderr, "-> Package %s not found\n", fullname.c_str());
		return -1;
	}

	stringstream ss;
	ss << directory << '/';
	ss << name << '-' << version;
	if (isAar())
		ss << ".aar";
	else
		// ".jar" is the default
		ss << ".jar";
	const string& filename = ss.str();

	ofstream fs(filename.c_str(), std::ios::out | std::ios::binary);
	if (!fs.is_open())
	{
		fprintf(stderr, "-> File \"%s\" could not be opened for writing\n", filename.c_str());
		return -1;
	}
	fs.write(reinterpret_cast<const char*>(&packageBytes[0]), packageBytes.size());
	fs.close();
	
	return 0;
}

void Package::getDependencies(vector<Package>& dependencies)
{
	const vector<char>& pomBytes = getPomBytes();
	if (!pomBytes.size()) return;

	xmlTextReaderPtr reader = xmlReaderForMemory(
		reinterpret_cast<const char*>(&pomBytes[0]),
		pomBytes.size(), "", NULL, 0);

	if (!reader) return;

	for (int ret = xmlTextReaderRead(reader); ret == 1; ret = xmlTextReaderRead(reader))
	{
		xmlNodePtr nodePtr = xmlTextReaderCurrentNode(reader);
		if (!nodePtr) continue;
		if (!nodePtr->name) continue;
		
		if (strcmp((const char*)nodePtr->name, "dependency")) continue;

		xmlChar* outerXml = xmlTextReaderReadOuterXml(reader);

		xmlTextReaderPtr outerReader = xmlReaderForMemory(
			(char*)outerXml, strlen((char*)outerXml), "", NULL, 0);

		if (!outerReader) return;

		string groupId;
		string artifactId;
		string version;
		for (int outerRet = xmlTextReaderRead(outerReader); outerRet == 1; outerRet = xmlTextReaderRead(outerReader))
		{
			xmlNodePtr outerNodePtr = xmlTextReaderCurrentNode(outerReader);
			if (!outerNodePtr) continue;
			if (!outerNodePtr->name) continue;
			if (!outerNodePtr->children) continue;

			if (!strcmp((const char*)outerNodePtr->name, "groupId"))
			{
				xmlChar* innerXml = xmlTextReaderReadInnerXml(outerReader);
				groupId = (char*)innerXml;
				xmlFree(innerXml);
			}
			else if (!strcmp((const char*)outerNodePtr->name, "artifactId"))
			{
				xmlChar* innerXml = xmlTextReaderReadInnerXml(outerReader);
				artifactId = (char*)innerXml;
				xmlFree(innerXml);
			}
			else if (!strcmp((const char*)outerNodePtr->name, "version"))
			{
				xmlChar* innerXml = xmlTextReaderReadInnerXml(outerReader);
				version = (char*)innerXml;
				xmlFree(innerXml);
			}
		}

		if ((groupId != "") && (artifactId != "") && (version != ""))
		{
			Package package(groupId, artifactId, version);
			dependencies.push_back(package);

			const string fullname = package.getFullName();
			printf("Depends on package %s\n", fullname.c_str());
		}

		xmlFree(outerXml);
	}
}

static vector<Package> packages =
{
	Package("com.android.support", "design", "25.0.0"),
	Package("com.android.support", "appcompat-v7", "25.0.0"),
	Package("com.android.support", "cardview-v7", "25.0.0"),
	Package("com.android.support", "support-vector-drawable", "25.0.0"),
	Package("com.android.support", "animated-vector-drawable", "25.0.0"),
	Package("com.wdullaer", "materialdatetimepicker", "2.5.0"),
	Package("org.greenrobot", "eventbus", "3.0.0"),
	Package("com.jakewharton", "butterknife-compiler", "8.2.1"),
	Package("com.jakewharton", "butterknife", "8.2.1"),
	Package("com.crashlytics.sdk.android", "crashlytics", "2.6.2"),
	Package("com.crashlytics.sdk.android", "answers", "1.3.9"),
};

static void processPackage(Package& package, vector<Package>& alreadySeenPackages)
{
	const string fullname = package.getFullName();
	if (package.storePackage("./"))
	{
		fprintf(stderr, "Could not process package %s\n", fullname.c_str());
		alreadySeenPackages.push_back(package);
		return;
	}
	alreadySeenPackages.push_back(package);
	printf("Downloaded package %s\n", fullname.c_str());
	
	vector<Package> dependencies;
	package.getDependencies(dependencies);
	
	for (int j = 0; j < dependencies.size(); j++)
	{
		Package& package = dependencies[j];
		if (find(alreadySeenPackages.begin(), alreadySeenPackages.end(), package) !=
			alreadySeenPackages.end())
			continue;
		
		processPackage(package, alreadySeenPackages);
	}
}

int main(int argc, char* argv[])
{	
	vector<Package> alreadySeenPackages;

	for (int i = 0; i < packages.size(); i++)
	{
		Package& package = packages[i];
		processPackage(package, alreadySeenPackages);
	}

	return 0;
}

