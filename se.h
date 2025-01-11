/** General macros. **/
/* array length. */
#define LEN(A) (sizeof(A)/sizeof(A[0]))

/* "Ctrl" key mask. */
#define CTR(C) (C & 0x1f)

/*
	There are two macros for converting integer macro to string.
	
	Extra level macro `STRCAST' is needed, because `#x' trick
	works only with macro *arguments*.
*/
#define STRCAST(x) #x
#define STR(x) STRCAST(x)

/*
	Public function prototypes.
	
	These are used in config.h.
*/
void quitses();
void togmod();
void sav();
void inslinebel();
void gbfb();
void gbff();
void gbfd();
void gbfu();
void gbfls();
void gbfle();
void gbfdb();
void gbfdf();
void gbfel();
void gbfsd();
void gbfsu();
void gbfdb();

/** Structs. **/
/*
	Keybinding.
	
	Map character `key' to function `func'.
	The array of these is defined in config.h.
*/
struct kbind {
	char key;
	void (*func)();
};
