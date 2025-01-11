static struct kbind kbinds[] = {
	/* key       function   */
	/* gracefully quit the editing session. */
	{ CTR('q'),   quitses    },
	/* toggle program modes. */
	{ CTR('j'),   togmod     },
	/* save the file. */
	{ CTR('w'),   sav        },
	/* insert a blanket new line below. */
	{ CTR('n'),   inslinebel },
	/* go left. */
	{ 'j',        gbfb       },
	/* go right. */
	{ ';',        gbff       },
	/* go down. */
	{ 'l',        gbfd       },
	/* go up. */
	{ 'k',        gbfu       },
	/* go to begining of line. */
	{ 'a',        gbfls      },
	/* go to line end. */
	{ 'd',        gbfle      },
	/* delete backward. */
	{ 's',        gbfdb      },
	/* delete forward. */
	{ 'f',        gbfdf      },
	/* erase current line. */
	{ 'e',        gbfel      },
	/* scroll one line down. */
	{ 'h',        gbfsd      },
	/* scroll one line up. */
	{ 'u',        gbfsu      },
	/*
		these two are for "Backspace" key and they
		delete one character backward.
	*/
	{  8 ,        gbfdb      },
	{ 127,        gbfdb      },
};
