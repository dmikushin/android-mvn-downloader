# C++ Apache Maven packages downloader

A lossy sketch of Apache Maven packages downloader written in C++ with CURL and libXML2.

Unlike mvn dependency plugin, which fails to download without an obvious reason, this downloader only fails due to unsupported features, such as "[2.6.1,)" kind of dependency version element syntax. The code is able to download .jar and .aar packages and recursively download dependencies. Has memory leaks that should be refactored using smart pointers.

## Deployment

Build with `make`, adjust the content of `repos` and `packages` vectors.

