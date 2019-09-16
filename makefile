all: android-mvn-downloader

android-mvn-downloader: android-mvn-downloader.cpp
	g++ -g -O3 -I/usr/include/libxml2 $< -o $@ -lcurl -lxml2

clean:
	rm -rf android-mvn-downloader

