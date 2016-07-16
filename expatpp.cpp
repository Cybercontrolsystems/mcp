// expatpp
#include "expatpp.h"

#include <assert.h>

expatpp::expatpp(bool createParser)
{
  if (createParser) {
  // subclasses may call this ctor after parser created!
		mParser = XML_ParserCreate(0);
		::XML_SetUserData(mParser, this);
		::XML_SetElementHandler(mParser, startElementCallback, endElementCallback);
		::XML_SetCharacterDataHandler(mParser, charDataCallback);
		::XML_SetProcessingInstructionHandler(mParser, processingInstructionCallback);
		::XML_SetDefaultHandler(mParser, defaultHandlerCallback);
		::XML_SetUnparsedEntityDeclHandler(mParser, unParsedEntityDeclCallback);
		::XML_SetNotationDeclHandler(mParser, notationDeclCallback);
		::XML_SetNotStandaloneHandler(mParser, notStandaloneHandlerCallback);
		::XML_SetNamespaceDeclHandler(mParser, startNamespaceCallback, endNamespaceCallback);
	}
}


expatpp::~expatpp()
{
	if (mParser)  // allows subclasses to avoid finishing parsing
	  ::XML_ParserFree(mParser);
}


void 
expatpp::startElementCallback(void *userData, const XML_Char* name, const XML_Char** atts)
{
	((expatpp*)userData)->startElement(name, atts);
}


void 
expatpp::endElementCallback(void *userData, const XML_Char* name)
{
	((expatpp*)userData)->endElement(name);
}


void 
expatpp::startNamespaceCallback(void *userData, const XML_Char* prefix, const XML_Char* uri)
{
	((expatpp*)userData)->startNamespace(prefix, uri);
}


void 
expatpp::endNamespaceCallback(void *userData, const XML_Char* prefix)
{
	((expatpp*)userData)->endNamespace(prefix);
}


void 
expatpp::charDataCallback(void *userData, const XML_Char* s, int len)
{
	((expatpp*)userData)->charData(s, len);
}


void
expatpp:: processingInstructionCallback(void *userData, const XML_Char* target, const XML_Char* data)
{
	((expatpp*)userData)->processingInstruction(target, data);
}


void
expatpp::defaultHandlerCallback(void* userData, const XML_Char* s, int len)
{
	((expatpp*)userData)->defaultHandler(s, len);
}


int
expatpp::notStandaloneHandlerCallback(void* userData)
{
	return ((expatpp*)userData)->notStandaloneHandler();
}


void
expatpp::unParsedEntityDeclCallback(void* userData, const XML_Char* entityName, const XML_Char* base, const XML_Char* systemId, const XML_Char* publicId, const XML_Char* notationName)
{
	((expatpp*)userData)->unparsedEntityDecl(entityName, base, systemId, publicId, notationName);
}


void
expatpp::notationDeclCallback(void *userData, const XML_Char* notationName, const XML_Char* base, const XML_Char* systemId, const XML_Char* publicId)
{
	((expatpp*)userData)->notationDecl(notationName, base, systemId, publicId);
}

//int
//expatpp::externalEntityRefCallback(XML_Parser parser, const XML_Char* openEntityNames, const XML_Char* base, const XML_Char* systemId, const XML_Char* publicId)
//{
//	((expatpp*)parser)->externalEntityRef(openEntityNames, base, systemId, publicId);
//}


void 
expatpp::startElement(const XML_Char*, const XML_Char**)
{}


void 
expatpp::endElement(const XML_Char*)
{}


void 
expatpp::startNamespace(const XML_Char* /* prefix */, const XML_Char* /* uri */)
{}


void 
expatpp::endNamespace(const XML_Char*)
{}


void 
expatpp::charData(const XML_Char*, int )
{
}


void
expatpp::processingInstruction(const XML_Char*, const XML_Char*)
{
}


void
expatpp::defaultHandler(const XML_Char*, int)
{
}


int
expatpp::notStandaloneHandler()
{
	return 0;
}


void
expatpp::unparsedEntityDecl(const XML_Char*, const XML_Char*, const XML_Char*, const XML_Char*, const XML_Char*)
{
}


void
expatpp::notationDecl(const XML_Char*, const XML_Char*, const XML_Char*, const XML_Char*)
{
}


int 
expatpp::skipWhiteSpace(const char* startFrom)
{
	// use our own XML definition of white space
	// TO DO - confirm this is correct!
	const char* s = startFrom;
	char c = *s;
	while ((c==' ') || (c=='\t') || (c=='\n') || (c=='\r')) {
		s++;
		c = *s;
	}
	const int numSkipped = s - startFrom;
	return numSkipped;
}


bool 
expatpp::emptyCharData(const XML_Char *s, int len)
{
// usually call from top of overriden charData methods
	if (len==0)
		return true;  //*** early exit - empty string, may never occur??
		
// skip newline and empty whitespace
	if (
		((len==1) && ( (s[0]=='\n') || (s[0]=='\r')) ) ||  // just CR or just LF
		((len==2) && (s[0]=='\r') && (s[1]=='\n'))  // DOS-style CRLF
	)
		return true;  //*** early exit - newline
		
	const int lastCharAt = len-1;
	if (s[lastCharAt]==' ') {  // maybe all whitespace
		int i;
		for (i=0; i<lastCharAt; i++) {
			if (s[i]!=' ')
				break;
		}
		if (i==lastCharAt)
			return true;	  //*** early exit - all spaces
	}
	return false;
}


// -------------------------------------------------------
//      e x p a t p p N e s t i n g
// -------------------------------------------------------
expatppNesting::expatppNesting() :
	mDepth(0),
	mParent(0)
{
// WARNING
// the assumption that is not obvious here is that if you want to use 
// nested parsers, then your topmost parser must also be an expatppNesting
// subclass, NOT an expatpp subclass, because we need the following special
// callbacks to override those in the expatpp ctor
	::XML_SetElementHandler(mParser, nestedStartElementCallback, nestedEndElementCallback);
}



expatppNesting::expatppNesting(expatppNesting* parent) :
	expatpp(false),  // don't create parser - we're taking over from inParent
	mDepth(0),
	mParent(parent)
{
	mParser = parent->mParser;
	assert(mParser);
	::XML_SetUserData(mParser, this);
}


expatppNesting::~expatppNesting()
{
	assert(!mParent);  // if we are a sub-parser, should not delete without calling returnToParent
}



expatppNesting* 
expatppNesting::returnToParent()
{
	expatppNesting* ret = mParent;
	::XML_SetUserData(mParser, mParent);
	mParent=0;
	mParser=0;  // prevent parser shutdown!!
	delete this;  // MUST BE LAST THING CALLED IN NON-VIRTUAL FUNCTION
	return ret;
}


void 
expatppNesting::nestedStartElementCallback(void *userData, const XML_Char* name, const XML_Char** atts)
{
	expatppNesting* nestedParser = (expatppNesting*)userData;
	nestedParser->mDepth++;
	((expatpp*)userData)->startElement(name, atts);  // probably user override
}


void 
expatppNesting::nestedEndElementCallback(void *userData, const XML_Char* name)
{
	expatppNesting* nestedParser = (expatppNesting*)userData;
// we don't know until we hit a closing tag 'outside' us that our run is done 	
	if (nestedParser->mDepth==0) {
		expatppNesting* parentParser = nestedParser->returnToParent();
		nestedEndElementCallback(parentParser, name);   // callbacks for expatppNesting stay registered, so safe 
		//if we don't invoke their callback, they will not balance their mDepth		
	}
	else {
	// end of an element this parser has started
		nestedParser->endElement(name);  // probably user override
		nestedParser->mDepth--;
	}
}

