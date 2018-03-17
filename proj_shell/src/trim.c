

/**
 * this file is help trim function
 *
 * @origin http://mwultong.blogspot.com/2007/06/c-trim-ltrim-rtrim.html
 */

/* Original Source Author : Joe Wright (12/06/2006) */

#include "trim.h"

const int MAX_STR_LEN = 4000;

char* rtrim(char* s) {

	char t[MAX_STR_LEN];
	char *end;

	strcpy(t, s);
	end = t + strlen(t) - 1;

	while (end != t && isspace(*end)) {
		end--;
	}

	*(end + 1) = '\0';
	s = t;
			    
	return s;
}


char* ltrim(char *s) {
	char* begin;
	begin = s;

	while (*begin != '\0') {
		if (isspace(*begin)) {
			begin++;
		} else {
			s = begin;
			break;
		}
	}

	return s;
}

char* trim(char *s) {  
	return rtrim(ltrim(s));
}


