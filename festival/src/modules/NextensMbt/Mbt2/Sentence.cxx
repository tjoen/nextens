/*
 *  Sentence.cc
 *
 *    The Sentence Class
 *
 * Copyright (c) 1998-2005
 * ILK  -  Tilburg University
 * CNTS -  University of Antwerp
 *
 * All rights Reserved.
 *
 */


#include <fstream> 
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cctype>
#include <cctype>
#include <cassert>

#include "Common.h"
#include "Pattern.h"
#include "Tree.h"
#include "Sentence.h"

#if defined(PTHREADS)

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>

#endif

namespace Tagger {
  using namespace Hash;
  using namespace Common;
  using namespace std;
 
  const string Separators = "\t \n";
  const int MAXTCPBUF   = 65536;

  // New enriched word.
  //
  word::word( const string& some_word, const vector<string>& extra_features, const string& some_tag){
      the_word =  some_word;
      word_tag = some_tag;
      the_word_index = -1;
      extraFeatures = extra_features;
  }  
 
  // Delete a word
  //
  word::~word(){
  }
  
  // New sentence.
  //
  sentence::sentence(){
    UTAG = -1;
    no_words = 0;
    for ( int i=0; i < MAX_WORDS; i++ )
      Words[i] = NULL;
  }
  
  // Delete it.
  //
  sentence::~sentence(){
    for ( int i=0; i < no_words; i++ )
      delete Words[i];
  }

  string sentence::getenr( int index ){
    string result;
    if ( index >= 0 && index < no_words ){
      vector<string>::const_iterator it=Words[index]->extraFeatures.begin();
      while( it != Words[index]->extraFeatures.end() ){
	result += *it;
	++it;
	if (  it != Words[index]->extraFeatures.end() )
	  result += " ";
      }
    }
    return result;
  }

  string sentence::Eos() const {
    if ( InternalEosMark == "EL" )
      return "\n\n";
    if ( InternalEosMark == "NL" )
      return "\n";
    return InternalEosMark; 
  }

  // Print it.
  //
  void sentence::print( ostream &os ) const{
    os << "Sentence :'";
    for ( int i = 0; i < no_words-1; i++ )
      os << Words[i]->the_word << ", ";
    if ( no_words > 0 ){
      os << Words[no_words-1]->the_word;
    }
    os << "'" << endl;
  }
  
  void sentence::reset( const string& EosMark ){
    // cleanup the sentence for re-use...
    for ( int i=0; i < no_words; i++ )
      delete Words[i];
    no_words = 0;
    InternalEosMark = EosMark;
  }
  
  bool sentence::Utt_Terminator( const string& z_something ){
    return ( z_something == InternalEosMark );
  }
  
  // Add an enriched word to a sentence.
  //
  void sentence::add( const string& a_word, 
		      const vector<string>& extraFeatures,
		      const string& a_tag ){
    Words[no_words++] = new word( a_word, extraFeatures, a_tag );
    if ( no_words >= MAX_WORDS ){
      cerr << "ERROR: too many words in sentence!" << endl;
      exit( -1 );
      //    cerr << "add word[" << no_words-1 << "] = " 
      //         << Words[no_words-1]->the_word << endl;
    }
  }
  
  // Add a word to a sentence.
  //
  void sentence::add(const string& a_word, const string& a_tag)
  {
    vector<string> tmp;
    add(a_word, tmp, a_tag);
  }  
  
  bool sentence::init_windowing( PatTemplate *Ktmpl,
				 PatTemplate *Utmpl, 
				 Lexicon &lex,
				 StringHash& TheLex ){
    if ( UTAG == -1 )
      UTAG = TheLex.Hash( UNKNOWN );
    if ( no_words == 0 ) {
      //    cerr << "ERROR: empty sentence?!" << endl;
      return false;
    }
    else {
      LexInfo * foundInfo;
      Ktemplate = Ktmpl;
      Utemplate = Utmpl;
      word *cur_word;
      for ( int wpos = 0; wpos < no_words; wpos++ ){
	cur_word = Words[wpos];
	cur_word->the_word_index = TheLex.Hash( cur_word->the_word );
	// look up ambiguous tag in the dictionary
	//
	foundInfo = lex.Lookup( cur_word->the_word );
	if( foundInfo != NULL ){
	  //	  cerr << "MT Lookup(" << cur_word->the_word << ") gave " << *foundInfo << endl;
	  cur_word->word_amb_tag = TheLex.Hash( foundInfo->Trans() );
	}
	else  {
	  // cerr << "MT Lookup(" << cur_word->the_word << ") gave NILL" << endl;
	  // not found, so give the appropriate unknown word code
	  cur_word->word_amb_tag = UTAG;
	}
      };
      return true;
    }
  }
  
  int sentence::classify_hapax( const string& word, StringHash& TheLex ){
    string hap = "HAPAX-";
    if ( word.find( "-" ) != string::npos ) // hyphen anywere
      hap += 'H';
    if ( isupper( word[0] ) ){ // Capitalized first letter?
      hap += 'C';
    }
    if ( word.find_first_of( "0123456789" ) != string::npos ) // digit anywhere
      hap += 'N';
    if ( hap.length() == 6 )
      hap += '0';
    return TheLex.Hash( hap );
  }
  
  bool sentence::nextpat( MatchAction *Action, vector<int>& Pat, 
			  StringHash& wordlist, StringHash& TheLex,
			  int position, int *old_pat ) {
    // safety check:
    //
    if( no_words == 0 || position >= no_words)
      return false;
    word *current_word = Words[position];
    unsigned int CurWLen = current_word->the_word.length();
    int i_feature=0;
    PatTemplate *aTemplate;
    word* wPtr;
    unsigned int tok;
    // is the present pattern for a known or unknown word?
    //
    if( *Action == MakeKnown )
      aTemplate = Ktemplate;
    else if ( *Action == MakeUnknown )
      aTemplate = Utemplate;
    else if( current_word->word_amb_tag == UTAG ){
      *Action = Unknown;
      //      cerr << "Next pat, Unknown word = "
      //  	 << current_word->the_word << endl;
      aTemplate = Utemplate;
    }    
    else {
      *Action = Known;
      //      cerr << "Next pat, Known word = " 
      //  	 << current_word->the_word << endl;
      aTemplate = Ktemplate;
    }
    
    // Prefix?
    //
    if (aTemplate->numprefix > 0) {
      for ( unsigned int j = 0;
	    j < (unsigned int)aTemplate->numprefix; j++) {
	string addChars = "_";
	if ( j < CurWLen ) 
	  addChars += current_word->the_word[j];
	else 
	  addChars += '=';  // "_=" denotes "no value"
	Pat[i_feature] = TheLex.Hash( addChars );
	i_feature++;
      }
    }
    
    for ( int i = 0, c_pos = position - aTemplate->word_focuspos;
	  i < aTemplate->wordslots;
	  i++, c_pos++) {
      // Now loop.
      //
      // depending on the slot type, transfer the appropriate 
      // information to the next feature
      //
      if ( c_pos >= 0 && c_pos < no_words ) {
	wPtr = Words[c_pos];
	//
	// If a list is specified, check if wPtr->the_word is
	// allowed.
	//
	switch(aTemplate->word_templatestring[i]) {
	case 'w':
	  if ( wordlist.NumOfEntries() == 0 ){
	    Pat[i_feature] = wPtr->the_word_index;
	  }
	  else {
	    tok = wordlist.Lookup( wPtr->the_word );
	    //cerr << "known word Lookup(" << wPtr->the_word << ") gave " << tok << endl;	    
	    if ( tok ){
	      Pat[i_feature] = wPtr->the_word_index;
	    }
	    else 
	      Pat[i_feature] = classify_hapax(  wPtr->the_word, TheLex );
	  }
	  i_feature++;
	  break;                    
	}
      }
      else {   // Out of context.
	Pat[i_feature] = TheLex.Hash( DOT );
	i_feature++;
      }
    } // i
    
    // Lexical and Context features ?
    //
    for ( int ii = 0, cc_pos = position - aTemplate->focuspos;
	  ii < aTemplate->numslots;
	  ii++, cc_pos++ ) {
      
      // move a pointer to the position of the word that
      // should occupy the present template slot
      //
      if ( cc_pos >= 0 && cc_pos < no_words ) {
	wPtr = Words[cc_pos];
	// depending on the slot type, transfer the appropriate 
	// information to the next feature
	//
	switch(aTemplate->templatestring[ii]){
	case 'd':
	  if ( old_pat == 0 )
	    Pat[i_feature] = wPtr->word_ass_tag;
	  else {
	    // cerr << "bekijk old pat = " << position+ii-aTemplate->focuspos
	    //  << " - " << old_pat[position+ii-aTemplate->focuspos] << endl;
	    Pat[i_feature] = old_pat[position+ii-aTemplate->focuspos];
	  }
	  i_feature++;
	  break;                        
	case 'f':
	  Pat[i_feature] = wPtr->word_amb_tag;
	  i_feature++;
	  break;
	case 'F':
	  break;
	case 'a':
	  Pat[i_feature] = wPtr->word_amb_tag;
	  i_feature++;
	  break;
      }
      }
      else{   // Out of context.
	Pat[i_feature] = TheLex.Hash( DOT );
	i_feature++;
      }
    } // i
    
    // Suffix?
    //
    if (aTemplate->numsuffix > 0) {
      for ( unsigned int j = aTemplate->numsuffix; j > 0; j--) {
	string addChars = "_";
	if ( j <= CurWLen ) 
	  addChars  += current_word->the_word[CurWLen - j];
	else 
	  addChars += '=';
	Pat[i_feature] = TheLex.Hash( addChars );
	i_feature++;
      }
    }
    
    // Hyphen?
    //
    if (aTemplate->hyphen) {
      string addChars;
      if ( current_word->the_word.find('-') != string::npos )
	addChars = "_H";
      else
	addChars = "_0";
      Pat[i_feature] = TheLex.Hash( addChars );
      i_feature++;
    }
    
    // Capital (First Letter)?
    //
    if (aTemplate->capital) {
      string addChars = "_";
      if ( isupper(current_word->the_word[0]) )
	addChars += 'C';
      else 
	addChars += '0';
      Pat[i_feature] = TheLex.Hash( addChars );
      i_feature++;
    }
    
    // Numeric (somewhere in word)?
    //
    if (aTemplate->numeric) {
      string addChars = "_0";
      for ( unsigned int j = 0; j < CurWLen; j++) {
	if( isdigit(current_word->the_word[j]) ){
	  addChars[1] = 'N';
	  break;
	}
      }
      Pat[i_feature] = TheLex.Hash( addChars );
      i_feature++;
    }
    //    cerr << "next_pat: i_feature = " << i_feature << endl;
    //    for ( int bla = 0; bla < i_feature; bla++ )
    //      cerr << bla << " - " << Pat[bla] << endl;
    return true;
  }

  void sentence::assign_tag( int cat, int pos ){
    // safety check:
    //
    if( no_words > 0 && pos < no_words )
      Words[pos]->word_ass_tag = cat;
  }
  
  bool sentence::known( int i ){
    if( no_words > 0 && i >= 0 && i < no_words )
      return Words[i]->word_amb_tag != UTAG;
    else
      return false;
  }

#if defined( PTHREADS)
  int sock_read(int sockfd,char *str,size_t count){
    int total_count = 0;
    char last_read = 0;
    char *current_position = str;
    while (last_read != 10) {
      int bytes_read = read( sockfd, &last_read, 1 );
      if (bytes_read <= 0) {
	// The other side may have closed unexpectedly 
	return -1; 
      }
      if ( (total_count < (signed) count) && 
	   (last_read != 10) && (last_read !=13) ) {
	*current_position++ = last_read;
	total_count++;
      }
    }
    if (count > 0)
      *current_position = 0;
    return total_count;
  }
  
  bool read_line( int socknum, char *line, int Size ){
    // read a line from the socket
    if ( sock_read( socknum, line, Size ) < 0 ) {
    //    cerr << "connection lost" << endl;
      return false;
    }
    else {
      return true;
    }
  }
  
  bool sentence::read( int socknum, input_kind_type kind ){
    if ( kind == ENRICHED ){
      cerr << "Sorry Enriched inputformat not supported in servermode" << endl;
      return false;
    }
    else
      return read( socknum, kind == TAGGED );
  }

  bool sentence::read( int socknum, bool tagged ){
    // read a line from the socket; we assume that the input
    // is formatted by the client to one sentence per line
    // the client should also do the tokenization!
    
    char linebuffer[MAXTCPBUF];
    string line;
    // Read input
    if ( read_line( socknum, linebuffer, MAXTCPBUF ) ) {
      cerr << socknum << " - " << linebuffer << endl;
      line = linebuffer;
      if ( line.length() > 0 )
	return Fill( line, tagged );
      else
	return false;
    }
    else
      return false;
  }  
#else
  bool sentence::read( int, input_kind_type ){
    cerr << "Sorry, not implemented..." << endl;
    abort();
  }

  bool sentence::read( int, bool ){
    cerr << "Sorry, not implemented..." << endl;
    abort();
  }  
#endif

  word_stat sentence::get_word( istream& is, string& Word ){
    Word = "";
    if ( is ){
      is >> ws >> Word;
      if ( InternalEosMark == "EL" || InternalEosMark == "NL" ){
	char ch;
	while( isspace((ch=is.peek())) && ch != '\n' ) is.get(ch);
	// skip all whitespace exept '\n'
	if ( ch == '\n' ){
	  is.get(ch); // get the '\n'
	  if ( InternalEosMark == "NL" ){
	    return LAST_WORD;
	  }
	  else {
	    while( isspace((ch=is.peek())) && ch != '\n' ) is.get(ch);
	    // skip all whitespace exept '\n'
	    if ( is.peek() == '\n' ){ 
	      // so an empty line
	    is.get(ch);
	    return LAST_WORD;
	    }
	  }
	}
      }
      else {
	if ( Utt_Terminator(Word) ){
	  // stop if Utterance terminator found
	  return EOS_FOUND;
	}
      }
      is >> ws;
      return READ_MORE;
    }
    return NO_MORE_WORDS;
  }


  bool sentence::read( istream &infile, input_kind_type kind ){
    if ( kind == TAGGED ||
	 kind == UNTAGGED )
      return read( infile, kind == TAGGED );
    else
      return read( infile );
  }

  bool sentence::read( istream &infile, bool tagged ){
    // read a whole sentence either from file
    // A sentence can be delimited either by a period, an Eos marker
    // or EOF.
    
    string linebuffer = "";
    string Word;
    // get the word from the file
    //
    word_stat eos ;
    do {
      eos = READ_MORE;
      while( eos == READ_MORE && infile ){
	eos = get_word( infile, Word );
	//	cerr << "got Word '" << Word << "'" << endl;
	if ( eos == EOS_FOUND )
	  break;
	linebuffer = linebuffer + ' ' + Word;
	if ( eos == LAST_WORD )
	  break;
	if( tagged ){
	  // get the tag
	  //
	  if ( infile ){
	    eos = get_word( infile, Word );
	    //	    cerr << "got Tag '" << Word << "'" << endl;
	    linebuffer = linebuffer + ' ' + Word;
	  }
	  else{
	    break;
	  }
	}
      }
      //      cerr << "Eos = " << eos << endl;
    }  while ( linebuffer.length() == 0 && infile );
    if ( linebuffer.length() > 0 )
      return Fill( linebuffer, tagged );
    else
      return false;
  }
  
  bool sentence::read( istream &infile ){
    // read an enriched and tagged word from infile
    // it must be a one_liner
    //    cerr << "Reading enriched" << endl;
    string linebuffer;
    string Word;
    string Tag;
    vector<string> extras;
    bool go_on = infile;
    while( go_on && infile ) {
      getline( infile, linebuffer );
      infile >> ws;
      int size = split( linebuffer, extras );
      if ( size == 1 && Utt_Terminator( extras.front() ) ){
	extras.clear();
	go_on = false;
      }
      if ( size >= 2 ){
	Word = extras.front();
	extras.erase(extras.begin());
	Tag  = extras.back();
	extras.pop_back();
	if ( !Word.empty() && !Tag.empty() ){
	  add( Word, extras, Tag );
	}
      }
    };
    //    print( cerr );
    return no_words > 0;
  }
  
  bool sentence::Fill( const string& line, bool tagged ){
    string token,  tagtoken;
    bool result = true;

    string::size_type s_pos, e_pos;
    s_pos = line.find_first_not_of( Separators );
    // in this loop we extract word (or word-tag pairs) from the buffer
    // and fill them into the "sentence" variable
    //
    while( s_pos != string::npos ){
      e_pos = line.find_first_of( Separators, s_pos );
      token  = line.substr( s_pos, e_pos - s_pos ); 
      // if necessary ... read the tag as well ...
      //
      s_pos = line.find_first_not_of( Separators, e_pos );
      if( tagged ) {
	if ( s_pos == string::npos ) {
	  cerr << "cannot get tag for word " << token << endl;
	  result = false;
	  break;
	}
	else {
	  e_pos = line.find_first_of( Separators, s_pos );
	  tagtoken  = line.substr( s_pos, e_pos - s_pos ); 
	  s_pos = line.find_first_not_of( Separators, e_pos );
	  // add the extracted token to the sentence representation
	  // there is an associated tag for the word
	  add( token, tagtoken );
	}
      }
      else {
	// add the extracted token(s) to the sentence representation
	// there is no associated tag for the word
	add( token, "" ); 
      }
    }
    return result;
  }
  
} // namespace
