/*
	se -- simple visual text editor.
	
	it works in two distinct modes:
	 - so called "0" (zero) mode, in which we can do navigation
	 - and so called "1" (one) mode - the editing mode.
	
	the following key bindings are supported:
	 alt + j - switch modes.
		
	 Bindings for "0" mode:
	  j - cursor left.
	  k - cursor top.
	  l - cursor down.
	  ; - cursor right.
	  a - move cursor to line start.
	  d - move cursor to line end.
	  f - delete one character forward.
	  s - delete one character backward.
	  e - erase whole current line.
		
	 Bindings for both "0" and "1" modes:
	  ctrl + s - save buffer into original file.
	  ctrl + q - exit program with saving.
	  alt + q - exit program without saving the buffer;
	            no warning will be printed.
	
	
	Please, also notice that `se' is a *text editor*, not a
	"file creator", so it will *not* create an unexisting file
	for you; i.e. when you type `se dummy' and file "dummy" doesn't
	exist, don't expect it to create it and open a newly created empty file.
	
	When invoked without arguments (i.e. just `se'), it enters the
	"isolated mode" - no actual file will be created and any attempts
	to save the file will be discarded. Consider this mode as a "sandbox".
	
	This program is designing and developing with an intention to be run
	and run nice both in bare console and in the terminal emulator under
	the window system (e.g. see X(7)).
*/


#include<termios.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/ioctl.h>
#include<stdlib.h>
#include<signal.h>
#include<stdarg.h> /* va_arg(3). */
#include<string.h>
#include<stdio.h>


/** General macros. **/
/*
	macro for allocating memory and checking if it has been successful.
	I - identifer
	S - size for alloction (as in malloc(3))
	P - name of parent function (for logging)
	L - length of parent function name plus identifier name length.
*/
#define A(I,S,P,L)if(!(I=malloc(S))){write(2,"cant alloc "#P"#"#I".\n",14+L);return 1;}

/* macro like the one above,but for realloc. */
#define R(I,T,S,P,L)if(!(I=realloc(T,S))){write(2,"cant realloc "#P"#"#I".\n",16+L);return 1;}

/* exit(3) versions of macros above. */
#define AE(I,S,P,L)if(!(I=malloc(S))){write(2,"cant alloc "#P"#"#I".\n",14+L);exit(1);}

#define RE(I,T,S,P,L)if(!(I=realloc(T,S))){write(2,"cant realloc "#P"#"#I".\n",16+L);exit(1);}
#define EE(C,M,L)if(C){write(2,#M,L);exit(1);}

/*
	There are two macros for converting integer macro to string.
	
	Extra level macro `STR_HELPER' is needed, because `#x' trick
	works only with macro *arguments*.
*/
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

/** Terminal-specific macros. **/
/*
	macro for handling ambiguous characters in main loop.
	C - character
	F - function it executes it "0" mod.
*/
#define AC(C,F)case C:{if(!mod){F();break;}goto pcl;}

/* move cursor to position (row, col). */
#define MVPS(R,C)"\x1b["#R";"#C"H"

/* move cursor up. */
#define MVU "\x1b[A"

/* move cursor down. */
#define MVD "\x1b[B"

/* move cursor left. */
#define MVL "\x1b[D"

/* move cursor right. */
#define MVR "\x1b[C"

/* buffer start row (integer). */
#define BFST 2

/* move cursor to the top-left corner. */
#define MVTL "\x1b[0;0H"

/* move to buffer start. */
#define MVBFST "\x1b[" STR(BFST) ";0H"

/* erase forward to the end of screen. */
#define ERSF "\x1b[J"

/* erase all. */
#define ERSA "\x1b[2J"

/* erase line forward. */
#define ERSLF "\x1b[K"

/* erase entire line. */
#define ERSLA "\x1b[2K"

/* video reset. */
#define VRST "\033[0m"

/* reverse video (it's used for reversing bg and fg colors). */
#define RVID "\033[7m"

/* mask for detecting CTRL key. */
#define CTR(C)(C & 0x1f)

/* initial gap size for gap buffer (see `gbf'). */
#define BFISZ 2

/* buffer expand size. */
#define BFXPNS 32

/* read-write buffer size. */
#define RWBFSZ 4096

/*
	write text in reverse video mode.
	S - string to write
	L - length of `S'.
*/
#define WRVID(S,L)(write(1,RVID,4),write(1,S,L),write(1,VRST,4))

/*
	sync terminal visual cursor with buffer cursor position.
	"terminal visual cursor" - what we see on the screen
	"buffer cursor position" - the data about cursor inside the program.
*/
#define SYCUR()scur(row,col)

/* one horizontal tab occupies the same space as `T' whitespaces. */
#define T 6

/* debug print. FOR DEBUG ONLY. */
#define DP(...)dprintf(2,__VA_ARGS__)


/** Structures. **/
/*
	gap buffer - the main data structure for edited text representation.
	 `sz' - size (with gap inclusively)
	 `a' - array
	 `gst' - gap start index
	 `gsz' - gap size.
*/
typedef struct {
	size_t sz;
	char* a;
	int gst;
	size_t gsz;
} gbf;

/*
	original `termios' structure.
	we'll use it to restore original terminal settings when program is closed.
	
	see termios(4).
*/
struct termios otos;

/* actual `termios'. */
struct termios tos;

/*
	terminal window size.
	
	see ioctl(2), tty(4).
*/
struct winsize wsz;


/** Variables. **/
/* the text buffer (gap buffer) (only one per editing session). */
gbf bf;

/* topmost buffer line (for scroll). */
int bfl;
/* cursor row and column. */
int row, col;
/* length of a target file name. see `fnm' and `fph'. */
int fnml;

/* editor mode. `0' - navigate `1' - edit. */
char mod;
/* has buffer been touched (has any change been made since last save). */
char tcht;
/* is "isolated mode" (when `se' invoked without a target file). */
char iso;

/* path of an edited file. */
char* fph;
/* filename (basename) of a target file. */
char* fnm;


void
die(char* err, ...) {
	va_list ap;
	va_start(ap, err);
	dprintf(2, "[se]: ");
	vdprintf(2, err, ap);
	va_end(ap);
	exit(1);
}

/**DBG print buffer. */
void
pbuf() {
	int i = 0;
	while(i<bf.sz){
		if (i == bf.gst || i == bf.gst+bf.gsz) DP("======\n");
		if (i >= bf.gst && i < bf.gst+bf.gsz) DP("[%d]:#\n", i);
		else if (bf.a[i] == '\t') DP("[%d]:\\t\n", i);
		else if (bf.a[i] == '\n') DP("[%d]:\\n\n", i);
		else if (bf.a[i] == ' ') DP("[%d]:\\s\n", i);
		else DP("[%d]:%c\n", i, bf.a[i]);
		++i;
	}
}

/* cast integer to string. */
void
itos(unsigned char x, char** s) {
	/* integer length (number of digits). */
	int l = 1;
	/* temp variable for counting length. */
	int z = x;
	/* current string index. */
	int i = 1;
	
	/* calculate integer length. */
	while (z /= 10) ++l;
	
	/* alloc memory for result string. */
	AE(*s, l+1, itos, 6);
	
	/*
		start building up a string from end to begining.
		so we start from the null byte.
	*/
	(*s)[l] = 0;
	do {
		/* convert digit to ASCII character. */
		(*s)[l-i] = (x % 10) + '0';
		x /= 10;
		++i;
	} while(x);
}

/* set terminal cursor to row and column (visually only). */
void
scur(unsigned char r, unsigned char c) {
	/* row string and column string. */
	char* rs=0;
	char* cs=0;
	
	/* string carrying the command to set the terminal cursor. */
	char* s=0;
	
	/*command-string length.*/
	int sl;
	
	/* convert row and column to strings. */
	itos(r, &rs);
	itos(c, &cs);
	
	/*
		calculate the command length.
		`4' stands for "\x1b[" + ";" + "H".
	*/
	sl = strlen(rs) + strlen(cs) + 4;
	
	/* allocate memory for command string (+null byte). */
	AE(s, sl+1, scur, 5);
	
	/* building a command string. */
	strcpy(s, "\x1b[");
	strcat(s, rs);
	strcat(s, ";");
	strcat(s, cs);
	strcat(s, "H");
	
	/* write it to stdout in order to send it to a terminal. */
	write(1, s, sl);
	
	free(rs);
	free(cs);
}

/* update filename. */
void
updfnm() {
	/* activate reverse video mode. */
	write(1, RVID, 4);
	
	/* mark touched buffer with asterisk. */
	if (!iso && tcht) write(1,"*",1);
	
	write(1,fnm,fnml);
	
	/* deactivate reverse video mode. */
	write(1,VRST,4);
}

/* update file touched status. */
void
updfnmtcht(char s) {
	tcht = s;
	
	/* set cursor to the very start and redraw the title. */
	write(1, MVPS(1,3) ERSLF, 9);
	updfnm();
	
	SYCUR();
}

/* initialize the gap buffer. */
void
gbfini() {
	/* set buf size to predefined one. */
	bf.sz = BFISZ;
	
	/* allocate memory for buffer array. */
	AE(bf.a, BFISZ, gbfini, 10);
	
	/* place gap at zero index. */
	bf.gst=0;
	bf.gsz=BFISZ;
}

/*
	expand gap buffer with delta `d'.
	this is used when all the gap space was filled
	and we have further input (i.e need more gap).
*/
void
gbfxpnd(int d) {
	/*
		how many characters we'll need to shift in
		order to place more gaps.
	*/
	size_t b = bf.sz - bf.gst;
	
	/* increase size variable. */
	bf.sz += d;
	
	/*
		increase gap variable;
		when we're expanding buffer - we simply add more gaps,
		so if buffer is expanded by 3, hence gap size is now 3 too
		(we assume we'll call this function only when current
		gap size is 0).
	*/
	bf.gsz += d;
	
	/* realloc buffer array to new size. */
	RE(bf.a, bf.a, bf.sz, gbfxpnd, 11);
	
	/*
		copy all the array contents starting from gap position to the
		very end of arr (for contents last char to be the last char
		in the array) in order to fill the "holes" appear after realloc.
		we do not need to swap holes and content as long as we can treat
		everything placed within gap boundaries as trash.
		
		example:
		 1) [1, 2, ()3, 4, 5]
		 	here `sz' = 5, `gst' = 2, `gsz' = 0.
		    now we want to expand by 5.
		 
		 2) [1, 2, (3, 4, 5, 0, 0), 0, 0, 0]
		    here `sz' = 10, `gst' = 2, `gsz' = 5.
		    zeroes are random data produced by `realloc'.
		 
		 3) [1, 2, (3, 4, 5, 0, 0), 3, 4, 5]
		    now content without the `()' is [1, 2, 3, 4, 5],
		    just like it was it the begining.
	*/
	memcpy(bf.a + bf.sz - b, bf.a + bf.gst, b);
}

/* insert a single character `c' into buffer. */
void
gbfinsc(char c) {
	/* if there is no enough space for gap-expand buffer. */
	if (!bf.gsz) gbfxpnd(bf.gsz+BFXPNS+1);
	
	/* put the character *before* the gap. */
	bf.a[bf.gst] = c;
	
	/* move gap to the right (forward). */
	++bf.gst;
	--bf.gsz;
}

/* insert a string `s' into buffer. */
void
gbfinss(char* s) {
	int sl = strlen(s);
	
	/* if gap doesn't have enough space for string-expaind buffer. */
	if (bf.gsz < sl) gbfxpnd(sl-bf.gsz+BFXPNS);
	
	int i = 0;
	/* insert string characters one-by-one. */
	while (i < sl) {
		bf.a[bf.gst+i] = s[i];
		++i;
	}
	
	/* move gap to the right (forward). */
	bf.gst += sl;
	bf.gsz -= sl;
}

/*
	display (print) buffer from index `s' to `e' on the screen.
*/
void
gbfdpl(int s,int e){
	/*
		command string that will be written to stdout when
		it's completely ready.
	*/
	char* cmd;
	
	int i, j;
	/* current index withing the `cmd'. */
	int cmdi;
	/* `cmd' size. */
	int cmds;
	/* number of new lines. */
	int n;
	
	/*
		when start (`s') equals end (`e'), then nothing new is
		about to be printed,so we can quit the procedure right now.
	*/
	if (s == e) return;
	
	cmd = NULL;
	i = s;
	cmdi = 0;
	cmds = 0;
	n = 0;
	
	while (i < e) {
		if (i >= bf.gst && i < bf.gst+bf.gsz) {
			i = bf.gst+bf.gsz;
			continue;
		}
		
		if (cmdi >= cmds) {
			RE(cmd, cmd, cmds+=256, gbfdpl, 10);
		}
		
		/* handle "\n" character. */
		if (bf.a[i] == '\n'){
			++n;
			
			/* if we don't have enough space for ERSLF(\n\r?). */
			if (cmdi+5 >= cmds) {
				RE(cmd,cmd,cmds+=256,gbfdpl,10);
			}
			memcpy(cmd+cmdi, ERSLF, 3);
			cmdi += 3;
			
			if (n == wsz.ws_row-1) break;
			memcpy(cmd+cmdi, "\n\r", 2);
			cmdi += 2;
		}
		/* handle "\t" character. */
		else if(bf.a[i] == '\t') {
			if (cmdi + 3*T >= cmds) {
				RE(cmd, cmd, cmds+=256, gbfdpl, 10);
			}
			
			j = 0;
			while (j++ < T) {
				memcpy(cmd+cmdi, MVR, 3);
				cmdi += 3;
			}
		}
		else {
			cmd[cmdi++] = bf.a[i];
		}
		
		++i;
	}
	
	write(1, cmd, cmdi);
	SYCUR();
	free(cmd);
}

/* display whole buffer (see `gbfdpl'). */
void
gbfdpla() {
	/* put cursor in the begining. */
	write(1, MVBFST, 6);
	
	gbfdpl(0, bf.sz);
}

/* display rest of the buffer (see `gbfdpl'). */
void
gbfdplrst() {
	/* first, erase this "rest of the buffer". */
	write(1,ERSF,3);
	
	gbfdpl(bf.gst+bf.gsz, bf.sz);
}

/* display rest of the current line (see `gbfdpl'). */
void gbfdplrstl(){
	/* `s' and `e' arguments for `gbfdpl'. */
	int s ,e;
	
	/*
		erase rest of the current line so that we can
		print new content instead of it.
	*/
	write(1,ERSLF,3);
	
	/* starting from next visible symbol. */
	s = bf.gst + bf.gsz;
	
	/* and stop and the next newline. */
	e = s;
	while (e < bf.sz && bf.a[e] != '\n') ++e;
	
	gbfdpl(s,e);
}

/* delete the next character (forward). */
void
gbfdf() {
	/* first aftergap idx. */
	int agi;
	agi = bf.gst+bf.gsz;
	/* we can't delete forward if cursor is in the end of text. */
	if (agi == bf.sz) return;
	
	/*
		deleting forward is equal to shifting right
		gap boundary forward.
	*/
	++bf.gsz;
	
	/*
		if we've deleted "\n" character, then we need to move
		line below up and append it to the end. it requires us to
		redraw all the text after gap. we do not need to change row/col
		variables 'cause cursor is still staying on the same line.
	*/
	if (bf.a[agi] == '\n') gbfdplrst();
	/*
		if char we've deleted is *not* a "\n", then we need to
		redraw only current line.
	*/
	else gbfdplrstl();
	
	/* don't forget to mark the buffer as modified. */
	updfnmtcht(1);
}

/* delete character under the cursor (i.e. delete backward). */
void
gbfdb() {
	/*
		iterator and previous "\n" character or "zero" idx.
		their semantic meanings are not same.
		actually, if we're on the first text line `i' should
		point to `-1' to have the same meaning (index before first
		character in the line) in case when we're not on the first line,
		but it's easier to organize loop for `i' to be `0' eventually.
		so we'll just handle this case separately later.
	*/
	int i;
	
	/*
		we can't delete backward if cursor in the
		begining of the text.
	*/
	if (!bf.gst) return;
	
	/*
		deleting backward is equal to shifting
		left gap boundary backward.
	*/
	--bf.gst;
	++bf.gsz;
	
	/*
		if we've deleted "\n" character, then we need to move current
		line above and append it to the end of previous one.
	*/
	if (bf.a[bf.gst] == '\n') {
		i = bf.gst;
		/* find previous "\n" or "zero" index. */
		while (i > 0 && bf.a[--i] != '\n');
		/*
			we *do* need to change row/col, because
			now cursor should be on the line above.
		*/
		/* go line above. */
		--row;
		/*
			place cursor in the end of the previous line.
			`bf.gst - i' - for previous line length
			`+(!i)' - is a trick for handling that semantic
		          	difference described above.
		*/
		col = bf.gst - i +(!i);
		
		SYCUR();
		/* reprint everything after cursor. */
		gbfdplrst();
	}
	/* if we've deleted "\t". */
	else if(bf.a[bf.gst] == '\t') {
		/* mimic tabulation by visual spaces. */
		col -= T;
		
		SYCUR();
		gbfdplrstl();
	}
	/* if character we've deleted is neither "\n", nor "\t". */
	else {
		/* move cursor back one char horizontally. */
		--col;
		/*
			we don't necessary need to sync here
			(although we still can) 'cause in this
			case it's much simpler to move cursor left
			by appropriate escape-sequence.
		*/
		/* so do. */
		write(1,MVL,3);
		/*
			as far as we hasn't move any line, deletion
			operation affected only current line, so we
			need to redraw only it.
		*/
		gbfdplrstl();
	}
	
	/* buffer has been touched. */
	updfnmtcht(1);
}

/*
	jump to then index `j' so the character at this
	index position will be next to the gap and to the
	cursor as well.
	
	example:
		in order to jump to:
		 1) the begining of the file, do `gbfj(0)'.
		 2) the end of the file - `gbfj(bf.sz)'.
*/
void
gbfj(int j) {
	/*
		These variables are needed in case we're
		jumping forward.
	*/
	int e, d;
	
	/*
		We cannot jump to the index if this index either:
		 1) less than 0 (out of text).
		 2) more than buffer size (out of text).
		 3) if the index is our current position.
		    well, actually, we can, but it wouldn't make any sense.
		 4) index lies within the gap (in the trash area).
	*/
	if (
	 j < 0 || j > bf.sz || j == bf.gst
	 || (j > bf.gst && j < bf.gst+bf.gsz)
	) return;
	
	/*
		Jumping backward.
		
		Example:
		 initial state:
		   [h, e, j, \n, j, o, s, {#, #}, e, f].
		 state after `gbfj(2)':
		   [h, e, {j, \n}, j, \n, j, o, s, e, f].
	*/
	if (j < bf.gst) {
		memmove(bf.a+j+bf.gsz, bf.a+j, bf.gst-j);
		bf.gst = j;
	}
	/*
		Jumping forward.
		
		Example:
		 initial state:
		   [h, e, j, \n, {#, #}, j, o, s, e, f].
		 state after `gbfj(9)':
		   [h, e, j, \n, j, o, s, {o, s}, e, f].
	*/
	else {
		e = bf.gst+bf.gsz;
		d = j-e;
		memmove(bf.a+bf.gst, bf.a+e, d);
		bf.gst += d;
	}
}

/*
	erase the line cursor is currently on (including "\n").
*/
void
gbfel(){
	/* new gap start. */
	int s;
	/* new gap end. */
	int e;
	/* initial value for `e'. */
	int ie;
	
	/*
		interrupt the procedure, ifbuffer is empty
		(hence nothing will be erased anyway).
	*/
	if (!bf.gst && bf.gst+bf.gsz == bf.sz) return;
	
	/* initialize values (they are not actual ones). */
	s = bf.gst-1;
	e = bf.gst+bf.gsz;
	
	/* find `s'. */
	while (s != -1 && bf.a[s] != '\n') --s;
	
	/* find `e'. */
	while (e < bf.sz && bf.a[e] != '\n') ++e;
	/* remember initial `e' value. */
	ie = e;
	
	/*
		Now let's set new gap start.
	*/
	/* if we're about to erase *not* the first line. */
	if (s != -1) {
		if (e != bf.sz) ++s;
		else s = s+!s;
	}
	/* if we're going to erase the first line. */
	else ++s;
	
	/*
		Now set new gap end.
	*/
	if (e == bf.sz) --e;
	bf.gst = s;
	bf.gsz = e-s+1;
	col = 1;
	/*
		if we've just erased last line, it means that now
		we would go one line above.
		but we want to leave cursor steady if buffer
		is empty (i.e. we need to jump up only if
		line above exists).
	*/
	if (ie == bf.sz) {
		/* index of previous "\n" (can be `-1'). */
		int p;
		/* now find this `p'. */
		p = bf.gst-1;
		while (p != -1 && bf.a[p] != '\n') --p;
		if(bf.a[s] != '\n' && !p) p = -1;
		
		/* and jump to the previous line. */
		gbfj(p+1);
		/*
			if the line we've erased is the first one
			(we're at the second row), then we *don't* need
			to go one line up. In other cases, we of course do.
		*/
		if (row != 2) --row;
	}
	
	/*
		as far as we erase all the line including "\n" in
		the end of it, we have to redraw *the whole rest*
		of the buffer, because it shifts the visual structure.
	*/
	SYCUR();
	gbfdplrst();
	
	/* line has been erased, buffer is touched now. */
	updfnmtcht(1);
}

/* move cursor to next character (forward). */
void
gbff() {
	/* index of a next character (next to the gap actually). */
	int nxi;
	nxi = bf.gst+bf.gsz;
	
	/*
		if we're in the end of text we can't move
		cursor further.
	*/
	if (nxi == bf.sz) return;
	
	bf.a[bf.gst] = bf.a[nxi];
	/* if next character is "\n". */
	if(bf.a[bf.gst] == '\n') {
		++row;
		col=1;
		SYCUR();
	}
	/* if next character is "\t". */
	else if (bf.a[bf.gst] == '\t') {
		col += T;
		SYCUR();
	}
	/* if next character is regular. */
	else {
		++col;
		write(1,MVR,3);
	}
	
	++bf.gst;
	DP("forw:row:%d, col:%d\n",row,col);
}

/* move cursor to previous character (backward). */
void
gbfb() {
	/* we can't move backward if we're in the begining. */
	if (!bf.gst) return;
	
	bf.a[bf.gst+bf.gsz-1] = bf.a[bf.gst-1];
	--bf.gst;
	
	/* if previous character is "\n". */
	if (bf.a[bf.gst+bf.gsz] == '\n') {
		/* next char after previous "\n". */
		int i;
		/* number of tabs. */
		int t;
		
		--row;
		t=0;
		i = bf.gst;
		while (i > 0) {
			if (bf.a[i-1] == '\t') ++t;
			if (bf.a[i-1] == '\n') break;
			--i;
		}
		col = bf.gst-i+((T-1)*t)+1;
		SYCUR();
	}
	/* if previous character is "\t". */
	else if (bf.a[bf.gst+bf.gsz] == '\t') {
		col-=T;
		SYCUR();
	}
	/* previous character is regular. */
	else {
		--col;
		write(1,MVL,3);
	}
	DP("backw:row:%d, col:%d\n",row,col);
}

/* move cursor down (to the next line). */
void
gbfd() {
	int i;
	int j;
	/* new col position. */
	int c;
	/* previous col position. */
	int p;
	
	i = bf.gst+bf.gsz;
	j = 1;
	c = 1;
	p = 1;
	
	while (bf.a[i] != '\n') {
		if (i > bf.sz) return;
		++i;
	}
	while((i+j) < bf.sz && bf.a[i+j] != '\n') {
		if (bf.a[i+j] == '\t') c += T;
		else ++c;
		
		/*
			this is basically for tab character, because
			tab character takes T columns, (possibly more
			than 1) so we can place cursor either in the
			begining of tab or in the end of it. and we're
			going to do this depending on what's visually
			closer to current cursor position.
		*/
		if (col-p < c-col) {
			c = p;
			break;
		}
		p = c;
		++j;
	}
	
	gbfj(i+j);
	++row;
	if (c == col) write(1, MVD, 3);
	else {
		col = c;
		SYCUR();
	}
	DP("down:row:%d, col:%d\n",row,col);
}

/* move cursor up (to the previous line). */
void
gbfu() {
	int i;
	int j;
	/* new column. */
	int c;
	/* previous column. */
	int p;
	
	i=bf.gst;
	c=1;
	p=1;
	
	while (bf.a[--i] != '\n') {
		if (i < 0) return;
	}
	j = i;
	while (--j >= 0) {
		if (bf.a[j] == '\n') break;
	}
	
	--row;
	if(col>i-j){
		col=i-j;
		SYCUR();
		gbfj(j+col);
	}
	else{
		/*
			here we're doing pretty much the same
			thing as in `gbfd'.
		*/
		i=j+1;
		while(c < col) {
			if (bf.a[i] == '\t') c += T;
			else ++c;
			if (col-p < c-col) {
				c = p;
				break;
			}
			p = c;
			++i;
		}
		if (c == col) write(1, MVU, 3);
		else {
			col = c;
			SYCUR();
		}
		gbfj(i);
	}
	DP("up:row:%d, col:%d\n",row,col);
}

/* scroll the screen down. */
void
gbfsd() {
	++bfl;
	gbfdpla();
	if (row == BFST) {
		gbfd();
		--row;
	}
	else {
		gbfu();
		++row;
	}
}

/* scroll screen up. */
void
gbfsu() {
	/*
		we can not scroll up if this is the first
		line is already visible.
	*/
	if (!bfl) return;
	
	--bfl;
	gbfu();
	gbfdpla();
}

/* move cursor to the line end. */
void
gbfle() {
	/* index of next "\n" or end-of-file. */
	int i;
	/* tab count. */
	int tc;
	
	i = bf.gst+bf.gsz;
	tc = 0;
	
	while (bf.a[i] != '\n' && i < bf.sz) {
		if (bf.a[i] == '\t') ++tc;
		++i;
	}
	
	if (i != bf.gst+bf.gsz) {
		col += ((i-tc)-bf.gst-bf.gsz) + tc*T;
		SYCUR();
		gbfj(i);
	}
}

/*move cursor to the line start.*/
void
gbfls(){
	/*
		index of a character that is next to the
		previous "\n".
	*/
	int i;
	/* tab count. */
	int tc;
	
	/*
		if previous character is "\n", then next
		one is `bf.gst'.
	*/
	i = bf.gst;
	tc = 0;
	
	/* find the position of `i'. */
	while (i > 0 && bf.a[i-1] != '\n') {
		if (bf.a[i-1] == '\t') ++tc;
		--i;
	}
	
	if (i != bf.gst) {
		col -= bf.gst-i-tc + tc*T;
		SYCUR();
		gbfj(i);
	}
}

/*
	print character `c';
	i.e. insert it in the buffer and display it
	(both character and buffer).
	
	This is what happens when we hit the key in "1" mode.
*/
void
pc(unsigned char c) {
	gbfinsc(c);
	
	/*
		handle inserting "\n" separately,
		'cause it alters the visual text structure.
	*/
	if (c == '\n') {
		/*
			Ye, we need to redraw *everything* what's next
			in the buffer after inserting newline.
			So first, erase that.
		*/
		write(1, ERSF, 3);
		
		/* move cursor to the begining of next row. */
		++row;
		col = 1;
		SYCUR();
		
		gbfdplrst();
	}
	/* handle inserting "\t" separately too. */
	else if (c == '\t') {
		/*
			And too wee need to redraw everything after that moment.
		*/
		write(1, ERSLF, 3);
		
		col += T;
		SYCUR();
		
		gbfdplrstl();
	}
	/* here we handle all printable characters. */
	else{
		write(1, &c, 1);
		++col;
		/*
			we don't need to call `SYCUR' here,
			because writing a char into stdin has already
			moved cursor one position right.
		*/
		gbfdplrstl();
	}
	
	/* say that buffer is modified now. */
	updfnmtcht(1);
}

/* insert new line below and place cursor there. */
void
gbfilb() {
	gbfle();
	pc('\n');
}

/* terminate the program. */
void
trm() {
	free(bf.a);
	tcsetattr(0, TCSANOW, &otos);
}

/*
	update mode indicator;
	i.e. it's either `0' or `1'.
*/
void
updm() {
	write(1, MVTL, 6);
	WRVID((mod ? "1" : "0"), 1);
}

/* update everything (all). */
void
upda() {
	updm();
	write(1,MVR,3);
	if (!iso) updfnm();
	
	gbfdpla();
}

/*
	save the file;
	in "isolated mode" it does nothing.
*/
void
sv() {
	DP("SAVE!\n");
	
	int fd;
	char wbf[RWBFSZ];
	int csz;
	int ri;
	int rl;
	
	fd = open(fph, O_WRONLY, O_TRUNC);
	if (fd < 0) {
		die("can not open file %s for saving.\n", fph);
	}
	
	/* TODO: need a loop here. */
	csz = bf.gst>RWBFSZ ? RWBFSZ : bf.gst;
	memcpy(&wbf, bf.a, bf.gst);
	write(fd, &wbf, bf.gst);
	ri = bf.gst+bf.gsz;
	rl = bf.sz-ri;
	memcpy(&wbf, bf.a+ri, rl);
	write(fd, &wbf, rl);
	
	ftruncate(fd, bf.sz-bf.gsz);
	if (close(fd) < 0) {
		die("can not save file %s.\n", fph);
	}
	
	/* now, when we saved the file, reset "touched" flag. */
	updfnmtcht(0);
}

/**DBG clear sterr file. */
void
clerr() {
	write(2, ERSA, 4);
	write(2, MVTL, 6);
}

/* enter "raw" terminal mode. */
void rawt() {
	/* get current terminal settings. see tcggetattr(3). */
	tcgetattr(0, &tos);
	
	/*
		save the original terminal settings to restore
		them when we exit the editor.
	*/
	otos = tos;
	
	/* atexit(3) returns `0' if successful. */
	if (atexit(&trm)) {
		die("can not set up an exit function.\n");
	}
	
	/*
		Here is how we enter the "raw" terminal mode.
		
		Explore meanings for this (as well as other)
		defines (actually, flags) in termios(4).
	*/
	/*
		Here we're *disabling* (leading `~') following things:
		 `ECHO', `ECHONL' - in order to not to echo things we type back.
		 `ICANON' - read byte-by-byte instead of default line-by-line.
		 `ISIG' - to turn off default keybindings for signals
		          like `SIGINT' (CTRL + C).
	*/
	tos.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG);
	/*
		*Disable*:
		 `IXON' - software flow control. These are things like
		          CTRL + S (freeze) and CTRL + Q (unfreeze).
		 `ICRNL' - do not map `CR' ("\r") to `NL' ("\n").
	*/
	tos.c_iflag &= ~(IXON | ICRNL);
	/*
		Turn off output processing, so we need to manually send
		"\r\n" instead of just "\n" every time we want a new line.
	*/
	tos.c_oflag &= ~OPOST;
	/*
		`VMIN' is a minimum number of characters we need to read
		before read(2) will be able to return. So it allows us to
		read bytes one-by-one too.
	*/
	tos.c_cc[VMIN] = 1;
	/*
		`VTIME' is how much time should pass before read(2)
		will be able to return. We don't want to wait.
	*/
	tos.c_cc[VTIME] = 0;
	
	/* apply all the changes we've just done. */
	tcsetattr(0, TCSANOW, &tos);
}

/*
	the main function involves main loop.
*/
int
main(int argc, char** argv) {
	/*
		file descriptor for target file.
		we make use of it in "isolated mode" only.
	*/
	int fd;
	/* number of bytes read when we read from file. */
	ssize_t rb;
	/*
		a read buffer for a file;
		i.e. buffer, we read the target file to.
	*/
	char rbf[RWBFSZ];
	/*
		we read user input one-by-one byte, and this
		variables carries the value of this single byte.
	*/
	unsigned char c;
	
	wsz = (struct winsize) {0};
	bf = (gbf) {0};
	bfl = 0;
	mod = 0;
	iso = 0;
	tcht = 0;
	row = BFST;
	col = 1;
	
	if (argc > 2) {
		die("too many argumets.\n");
	}
	
	/*
		if no target file is specified, then enter "isolated mode".
		since in this case empty buffer is going to be created,
		it'll be nice to set editor mode to "1" (insert) by default.
	*/
	if (argc == 1){
		iso = 1;
		mod = 1;
	}
	
	if (ioctl(1, TIOCGWINSZ, &wsz) < 0) {
		die("can not perform an request for window size.\n");
	}
	
	gbfini();
	
	if (argc == 2) {
		/* `fph' (filepath) iterator. */
		int j;
		/* slash ("/") index. */
		int si;
		
		fph = argv[1];
		fd = open(fph, O_RDWR);
		if (fd < 0) {
			die("can not open file %s.\n", fph);
		}
		
		/*
			Let's find file basename.
		*/
		fnm = fph;
		j = 0;
		si = -1;
		
		/* find an index of last slash in the filepath. */
		while (fph[j] != 0) {
			if (fph[j] == '/') si = j;
			++j;
		}
		
		/* `-1' for excluding null-byte. */
		fnml = j-si-1;
		/* `+1' for including null-byte. */
		A(fnm, fnml+1, main, 8);
		
		/*
			Write filename base byte-by-byte.
		*/
		j = si;
		while (++j <= si+fnml+1) {
			fnm[j-si-1] = fph[j];
		}
		
		/*
			Read the contents of a target file and
			accumulate it inside the gap buffer.
			Initially, our *gap* is situated in the begining
			(at 0 index), so we put all the actual text after it.
		*/
		while((rb = read(fd, &rbf, RWBFSZ)) > 0) {
			R(bf.a,bf.a, bf.sz+rb, main, 8);
			memcpy(bf.a+bf.sz, &rbf, rb);
			bf.sz += rb;
		}
		
		if (rb < 0) {
			die("error during reading file %s.\n", fph);
		}
		if (close(fd) < 0) {
			die("can not close file %s.\n", fph);
		}
	}
	
	rawt();
	/* clear the screen. */
	write(1, ERSA, 4);
	upda();
	SYCUR();
	
	/* The main loop. */
	while(1) {
		if (read(0, &c, 1) > 0) {
			switch (c) {
			/*
				gracefully quit the program.
			*/
			case CTR('q'): {
				/*
					number of following "\n" visible on the screen;
					i.e. it's not necessarily the last one in the text.
				*/
				int n;
				/*
					iterator that is used to find
					out the `n' variable.
				*/
				int i;
				
				/*
					find number of visually remaining newlines.
				*/
				i = bf.gst+bf.gsz;
				n = 1;
				while (i < bf.sz && n < wsz.ws_row) {
					if (bf.a[i] == '\n') ++n;
					++i;
				}
				
				/*
					jump to the last line, erase it and quit.
				*/
				col = 1;
				row += n;
				SYCUR();
				write(1, ERSLF, 3);
				
				return 0;
			}
			/*
				Toggle modes ("0" and "1").
			*/
			case CTR('j'): {
				mod ^= 1;
				updm();
				SYCUR();
				break;
			}
			/*
				Save file.
				
				actually, we save file only if buffer has
				been modified since last save.
			*/
			case CTR('s'): {
				if (!iso && tcht) sv();
				break;
			}
			AC('n', gbfilb)
			case CTR('n'): {
				if (mod == 1) gbfilb();
				break;
			}
			/**DBG clear stderr file. */
			AC('\\', clerr)
			/**DBG print buffer. */
			AC(']', pbuf)
			AC('j', gbfb)
			AC(';', gbff)
			AC('l', gbfd)
			AC('k', gbfu)
			AC('a', gbfls)
			AC('d', gbfle)
			AC('s', gbfdb)
			AC('f', gbfdf)
			AC('e', gbfel)
			AC('h', gbfsd)
			AC('u', gbfsu)
			/* backspace. */
			case 8:
			case 127: {
				if (mod) gbfdb();
				break;
			}
			default: {
				/*
					"Enter" key generates 13 (`CR') character,
					but we want to make it 10 (`NL').
				*/
				if (c == '\r') c = '\n';
				
				/*
					if we're not in insert mode or we try
					to insert a non-alphabet character ("\n" exclusive)
					just do nothing.
				*/
				if (mod != 1 || ((c < 32 || c > 126)
				    && c != '\n' && c != '\t')) break;
				
				/* label for printing a character. */
				pcl: pc(c);
			}
			}
		}
	}
}
