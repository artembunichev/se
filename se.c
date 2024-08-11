/*se-stupid text editor.
alt+j-switch modes
mode 0(navigation+basic editing):
	j-cursor left,
	k-cursor top,
	l-cursor down,
	;-cursor right,
	a-move cursor to line start,
	d-move cursor to line end,
	f-delete one character forward,
	s-delete one character backward,
	e-erase whole current line.
mode 1-insert mode.
in any mode:
	ctrl+s-save buffer into original file.
	ctrl+q-exit program with saving.
	alt+q-exit program without saving the buffer
	(do not expect silly hints that you forgot to save the file).
when invoked without file as a target, isolated mode is entered.
it's almost the same as regular one, but buffer can not be saved into a
file in any way. it is a text editor,not a file-creator and it won't do this
stuff for you. so, if you'd like to create a new file, please be so kind as to
do this with touch(1) or whatever other program you like.*/
#include<termios.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/ioctl.h>
#include<stdlib.h>
#include<signal.h>
#include<errno.h>
#include<string.h>
#include<stdio.h>

/*macro for allocating memory+checking if it has been successfull.
identifer,
size for alloction,
name of parent function(for logging),
length of parent function name+identifier name length accumulatively.*/
#define A(I,S,P,L)if(!(I=malloc(S))){write(2,"cant alloc "#P"#"#I".\n",14+L);return 1;}
/*macro like the one above,but for realloc.*/
#define R(I,T,S,P,L)if(!(I=realloc(T,S))){write(2,"cant realloc "#P"#"#I".\n",16+L);return 1;}
/*error-if&termination macro.
condition,message,its length.*/
#define E(C,M,L)if(C){write(2,#M,L);return 1;}
/*exit() versions of macros above.*/
#define AE(I,S,P,L)if(!(I=malloc(S))){write(2,"cant alloc "#P"#"#I".\n",14+L);exit(1);}
#define RE(I,T,S,P,L)if(!(I=realloc(T,S))){write(2,"cant realloc "#P"#"#I".\n",16+L);exit(1);}
#define EE(C,M,L)if(C){write(2,#M,L);exit(1);}
/*macro for handling ambiguous characters in main loop.
character,its function in 0 mode.*/
#define AC(C,F)case C:{if(!mod){F();break;}goto pc;}
/*move cursor to position.*/
#define MVPS(R,C)"\x1b["#R";"#C"H"
/*move cursor up.*/
#define MVU "\x1b[A"
/*move cursor down.*/
#define MVD "\x1b[B"
/*move cursor left.*/
#define MVL "\x1b[D"
/*move cursor right.*/
#define MVR "\x1b[C"
#define BFSTR "2"
/*move cursor to the top-left corner.*/
#define MVTL "\x1b[0;0H"
/*move to buffer start.*/
#define MVBFST "\x1b[" BFSTR ";0H"
/*erase forward to the eof screen.*/
#define ERSF "\x1b[J"
/*erase all.*/
#define ERSA "\x1b[2J"
/*erase line forward.*/
#define ERSLF "\x1b[K"
/*erase entire line.*/
#define ERSLA "\x1b[2K"
/*video reset.*/
#define VRST "\033[0m"
/*reverse video (it's used for reversing bg and fg colors).*/
#define RVID "\033[7m"
/*mask for detecting CTRL.*/
#define CTR(C)(C&0x1f)
#define BFISZ 2
/*buffer expand size.*/
#define BFXPNS 32
/*readwrite buf size*/
#define RWBFSZ 4096
/*write text in reverse video mode.S-str,L-len.*/
#define WRVID(S,L)(write(1,RVID,4),write(1,S,L),write(1,VRST,4))
/*sync terminal visual cursor with buffer cursor position.*/
#define SYCUR()scur(row,col)
/*one tab(\t) the same size as (spaces).*/
#define T 6
/*debug print.FOR DEBUG ONLY.*/
#define DP(...)dprintf(2,__VA_ARGS__)

/*gap buffer.sz-size(gap inclusive) a-arr gst-gap start gsz-gap size.*/
typedef struct{
size_t sz;
char* a;
int gst;
size_t gsz;
}gbf;
/*original termios. we'll use it to restore original terminal settings.*/
struct termios otos;
/*term win size.*/
struct winsize wsz;
/*text buffer(the only one).*/
gbf bf;
int
/*topmost buffer line(for scroll).*/
bfl
/*cursor position.row and column respectively.*/
,row
,col
,fnml;/*filename length(see fnm below).*/
/*editor mode. 0-navigate 1-edit.*/
char mod
,tch/*has buffer been touched(has any change been made since last save).*/
,iso;/*is isolated mode(when executed without target file).*/
char* fph;/*file path of currently edited file.*/
char* fnm;/*filename of target file (differs from file path).*/

/*print buffer.
FOR DEBUG ONLY.*/
void
pbuf(){
int i;
i=0;
while(i<bf.sz){
if(i==bf.gst||i==bf.gst+bf.gsz)DP("======\n");
if(i>=bf.gst&&i<bf.gst+bf.gsz)DP("[%d]:#\n",i);
else if(bf.a[i]==9) DP("[%d]:\\t\n",i);
else if(bf.a[i]==10)DP("[%d]:\\n\n",i);
else if(bf.a[i]==32) DP("[%d]:\\s\n",i);
else DP("[%d]:%c\n",i,bf.a[i]);
++i;
}
}

/*own function for calculating string length.*/
int
strl(char* s){
int i/*string iterator.*/
,l;/*length of the string.*/
i=0;
l=0;
while(s[i]){++l;++i;}
return l;
}

void
itos(unsigned char x,char** s)/*integer to str.*/
{
int l/*integer length(number of digits).*/
,z/*tmp variable for counting length.*/
,i;/*current string idx.*/
l=1;
z=x;
i=1;
while(z/=10)l++;/*count integer length.*/
AE(*s,l+1,itos,6)/*alloc memory for result string.*/
(*s)[l]=0;/*start building up a string from the end.so we start from null byte.*/
do{(*s)[l-i]=(x%10)+48;/*convert digit to ascii char.*/
x/=10;
i++;/*go to next digit.*/
}while(x);
}

void
scur(unsigned char r,unsigned char c){/*set terminal cursor to row and column(only visual).*/
char* rs=0;/*row string.*/
char* cs=0;/*col string.*/
char* s=0;/*string carrying the command to set terminal cursor.*/
int sl;/*command-string length.*/
itos(r,&rs);itos(c,&cs);/*convert row and col to strings.*/
sl=strl(rs)+strl(cs)+4;/*calc command length(4 stands for \x1b[ + ; + H).*/
AE(s,sl+1,scur,5)/*alloc memory for command string (+null byte).*/
strcpy(s,"\x1b[");/*building a command string.*/
strcat(s,rs);
strcat(s,";");
strcat(s,cs);
strcat(s,"H");
/*write to stdout in order to send command to terminal
for it(term) to apply it(str).*/
write(1,s,sl);
free(rs);free(cs);/*free memory was reserved for row/col strings.*/
}

/*update filename.*/
void
updfnm(){
/*activate reverse video mode.*/
write(1,RVID,4);
/*mark touched buffer with asterisk.*/
if(!iso&&tch)write(1,"*",1);
write(1,fnm,fnml);
/*deactivate reverse video mode.*/
write(1,VRST,4);
}

/*update filename touched status.*/
void
updfnmtch(char s){/*status.*/
tch=s;
write(1,MVPS(1,3) ERSLF,9);
updfnm();
SYCUR();
}

/*gap buffer is a main structure for text buffer.*/
void
gbfini()/*init gap buffer.*/
{
	bf.sz=BFISZ;/*set buf size to predefined one.*/
	AE(bf.a,BFISZ,gbfini,10)/*alloc memory for buf array.*/
	bf.gst=0;bf.gsz=BFISZ;/*place gap at 0 idx.*/
}

/*expand buffer with delta. used the all the gap space was filled
and it doesn't seem to be the end of input.*/
void
gbfxpnd(int d)
{
	size_t b=bf.sz-bf.gst;/*how many characters we'll need to shift in order to place more gaps.*/
	bf.sz+=d;/*increase size variable.*/
	/*increase gap variable (when we're expanding buffer - we simply add more gaps, so if
	buffer is expanded by 3 hence gap size is now 3 too (we assume we'll call expand function only when
	current gap size is 0)).*/
	bf.gsz+=d;
	RE(bf.a,bf.a,bf.sz,gbfxpnd,11)/*realloc buffer array to new size.*/
	/*copy all arr content starting from gap pos to the very end of arr (for contents last char to be the
	last char in the arr) in order to fill the "holes" appear after realloc. we do not need to swap
	holes and content as long as we can treat everything placed within gap boundaries as trash
	example: [1,2,()3,4,5] - sz=5,gst=2,gsz=0 - we want to expand by 5
	         [1,2,(3,4,5,0,0),0,0,0] - sz=10,gst=2,gsz=5 - zeroes are random data produced by realloc
	         [1,2,(3,4,5,0,0),3,4,5] - now content without the () is [1,2,3,4,5] as it
	                                   was in the begining.*/
	memcpy(bf.a+bf.sz-b,bf.a+bf.gst,b);
}

void
gbfinsc(char c){/*insert char.*/
/*if there is no enough space for gap-expand buf.*/
if(!bf.gsz)gbfxpnd(bf.gsz+BFXPNS+1);
bf.a[bf.gst]=c;/*put char before the gap.*/
++bf.gst;--bf.gsz;/*move gap to the right(forward).*/
}

void
gbfinss(char* s){/*insert string.*/
int sl/*lenght of inserted string.*/
,i;
sl=strl(s);
/*if gap hasn't got enough space for string-expaind buf.*/
if(bf.gsz<sl)gbfxpnd(sl-bf.gsz+BFXPNS);
i=0;
while(i<sl)bf.a[bf.gst+i]=s[i++];/*insert str chars one by one.*/
bf.gst+=sl;bf.gsz-=sl;/*move gap to the right(forward).*/
}

/*display buffer(display \n as \n\r
without modifying original text).*/
void
gbfdpl(int i,int e)/*i-start position,e-end position.*/
{
int n
,r
,j
,wl
,k
,z;
n=0;
r=1;
k=0;
char* w;
while(n<bfl&&i<e){
if(i==bf.gst){i=bf.gst+bf.gsz;continue;}
if(bf.a[i]==10)++n;
++i;
}
j=i-1;
while(r<wsz.ws_row&&++j<bf.sz)if(bf.a[j]==10)++r;
wl=j-i+r;
AE(w,wl,gbfdpl,7)
z=i;
while(z<j){
if(bf.a[z]==10){
/*move term cursor to line start after every \n (mimic \r).*/
memcpy(w+k,"\n\r",2);
k+=2;
}
else{w[k]=bf.a[z];++k;}
++z;
}
memcpy(w+k,ERSF,3);
write(1,w,wl);
free(w);
SYCUR();
}

/*displat whole buffer.*/
void
gbfdpla()
{
	write(1,MVBFST,6);
	gbfdpl(0,bf.sz);
}

/*display rest of the buffer.*/
void
gbfdplrst(){
gbfdpl(bf.gst+bf.gsz,bf.sz);
}

/*display rest of the current line.*/
void gbfdplrstl(){
int u/*first aftergap idx.*/
,i;/*iterator and next \n or eof idx at the same time.*/
u=bf.gst+bf.gsz;
i=u;
while(i<bf.sz&&bf.a[i]!=10)++i;/*find idx of next \n or eof.*/
write(1,bf.a+u,i-u);/*print all the chars between gap end and closes \n or eof.*/
SYCUR();/*sync term cur 'cause it's now in the end of line.*/
}

void
gbfdf(){/*delete one char forward.*/
int agi;/*first aftergap idx.*/
agi=bf.gst+bf.gsz;
if(agi==bf.sz)return;/*we can't delete forward if cursor is in the end of text.*/
bf.gsz++;/*deleting forward is equal to shifting right gap boundary forward.*/
/*if we've deleted \n char then we need to move line below up and append it to the end. it requires us to
redraw all the text after gap. we do not need to change row/col vars 'cause cursor is still staying on the
same line.*/
if(bf.a[agi]==10){write(1,ERSF,3);gbfdplrst();}
/*if char we've deleted is not a \n then we need to redraw only current line.*/
else{write(1,ERSLF,3);gbfdplrstl();}
updfnmtch(1);/*don't forget to mark buffer as modified.*/
}

void
gbfdb(){/*delete one char backward.*/
int i;
if(!bf.gst)return;/*we can't delete backward if cursor in the begining of the text.*/
--bf.gst;++bf.gsz;/*deleting backward is equal to shifting left gap boundary backward.*/
/*if we've deleted \n char then we need to move current line above and append it to the end.*/
if(bf.a[bf.gst]==10){
/*iterator and prev \n or ZERO idx. their semantic meanings are not the same.
actually if we're on the first text line i should points to -1 to have the
same meaning(idx before first char in line) in case when we're not on the
first line, but it's easier to organize loop for i to be 0 eventually.
so we'll just handle this case separately later.*/
i=bf.gst;
while(i>0&&bf.a[--i]!=10);/*find prev \n or zero idx.*/
/*we do need to change row/col 'cause now cursor should be on the line above.*/
--row;/*go line above.*/
/*place cur in the end of previous line (bf.gst-i) is for prev line length
and +(!i) trick is for handling that semantic difference described above.*/
col=bf.gst-i+(!i);
SYCUR();/*sync term cursor with new row/col positions we've just set.*/
write(1,ERSF,3);/*erase everything after cursor.*/
gbfdplrst();/*reprint everything after cursor.*/
updfnmtch(1);/*buffer has been touched.*/
}
else if(bf.a[bf.gst]==9){/*if we've deleted \t.*/
col-=T;
write(1,ERSLF,3);
SYCUR();
gbfdplrstl();
} else{/*if char we've deleted is not a \n.*/
--col;/*move cursor back one char horizontally.*/
/*we don't necessary need to sync here (although we stll can) 'cause in
this case it's much simpler to move cursor left by appropriate esc-sequence.*/
write(1,MVL,3);/*so move cursor left.*/
/*as far as we didn't move any line, deletion operation affected only current
line, so we need to redraw only it.*/
write(1,ERSLF,3);
gbfdplrstl();
}
}

void
gbfj(int j)/*jump to idx.*/
{
int e;
int d;
if(j<0||j>bf.sz||j==bf.gst||(j>bf.gst&&j<bf.gst+bf.gsz))return;
if(j<bf.gst)
{
	memmove(bf.a+j+bf.gsz,bf.a+j,bf.gst-j);
	bf.gst=j;
}else{
	e=bf.gst+bf.gsz;
	d=j-e;
	memmove(bf.a+bf.gst,bf.a+e,d);
	bf.gst+=d;
}
}

/*was the hardest stuff for me.*/
void
gbfel(){/*erase the line cursor is currently on (\n including).*/
int s/*new gap start.*/
,e/*new gap end.*/
,ie;/*initial value of e(since e is going to be modified later).*/
s=bf.gst-1;
e=bf.gst+bf.gsz;
while(s>0&&bf.a[s]!=10)--s;/*find s.*/
while(e<bf.sz&&bf.a[e]!=10)++e;/*find e.*/
ie=e;/*remember initial e value.*/
/*set new gap start.*/
if(s!=-1){
	if(e!=bf.sz)++s;
	else s=s+!s;
}else ++s;
/*set new gap end.*/
if(e==bf.sz)--e;
bf.gst=s;
bf.gsz=e-s+1;
col=1;
/*if we've just erased last line, it means that now
we would go one line above.
but we want to leave cursor steady if buffer
is empty(actually, we need to jump up only if
line above exists).*/
if(ie==bf.sz){
int p;/*idx of previous \n(can be -1).*/
p=bf.gst-1;
while(p!=-1&&bf.a[p]!=10)--p;
if(bf.a[s]!=10&&!p)p=-1;
gbfj(p+1);
row!=2&&--row;
}
SYCUR();
write(1,ERSF,3);
gbfdplrst();
updfnmtch(1);/*line has been erased,buffer is touched now.*/
}

void
gbff(){/*move cursor forward.*/
int nxi;
nxi=bf.gst+bf.gsz;
if(nxi==bf.sz)return;
bf.a[bf.gst]=bf.a[nxi];
/*handle \n.*/
if(bf.a[bf.gst]==10){row++;col=1;SYCUR();}
/*handle \t.*/
else if(bf.a[bf.gst]==9){col+=T;SYCUR();}
else{col++;write(1,MVR,3);}
++bf.gst;
DP("forw:row:%d, col:%d\n",row,col);
}

void
gbfb(){/*move cursor backward.*/
if(!bf.gst)return;
bf.a[bf.gst+bf.gsz-1]=bf.a[bf.gst-1];
--bf.gst;
/*handle \n.*/
if(bf.a[bf.gst+bf.gsz]==10){
	int i;/*next char after previous \n.*/
	--row;
	i=bf.gst;
	while(i>0&&bf.a[i-1]!=10)--i;
	col=bf.gst-i+1;
	SYCUR();
/*handle \t.*/
}else if(bf.a[bf.gst+bf.gsz]==9){col-=T;SYCUR();}
else{
--col;
write(1,MVL,3);
}
DP("backw:row:%d, col:%d\n",row,col);
}

void
gbfd(){/*move cursor down.*/
int i
,j;
i=bf.gst+bf.gsz;
j=1;
while(bf.a[i]!=10){if(i>bf.sz)return;++i;}
while((i+j)<bf.sz&&bf.a[i+j]!=10&&j<col)++j;
gbfj(i+j);
++row;
if(j==col)write(1,MVD,3);
else{
col=j;
SYCUR();
}
DP("down:row:%d, col:%d\n",row,col);
}

void
gbfu(){/*move cursor up.*/
int i
,j;
i=bf.gst;
while(bf.a[--i]!=10)if(i<0)return;
j=i;
while(--j>=0&&bf.a[j]!=10);
--row;
if(col>i-j){col=i-j;SYCUR();}else write(1,MVU,3);
gbfj(j+col);
DP("up:row:%d, col:%d\n",row,col);
}

void
gbfsd()/*scroll screen down.*/
{
++bfl;gbfdpla();if(row==2){gbfd();--row;}else{gbfu();++row;}
}

void
gbfsu()/*scroll screen up.*/
{
if(!bfl)return;
--bfl;
gbfu();
gbfdpla();
}

/*move cursor to the line end.*/
void
gbfle(){
int i/*index of next \n or eof.*/
,tc;/*tab count.*/
i=bf.gst+bf.gsz;
tc=0;
while(bf.a[i]!=10&&i<bf.sz){if(bf.a[i]==9)++tc;++i;}
if(i!=bf.gst+bf.gsz){
col+=((i-tc)-bf.gst-bf.gsz)+tc*T;
SYCUR();
gbfj(i);
}
}

/*move cursor to the line start.*/
void
gbfls(){
int i/*idx of a character that is next to the previous \n.*/
,tc;/*tab count.*/
i=bf.gst;/*if previous character is \n then next one is bf.gst.*/
tc=0;
/*find the position of i.*/
while(i>0&&bf.a[i-1]!=10){if(bf.a[i-1]==9)++tc;--i;}
if(i!=bf.gst){
col-=bf.gst-i-tc+tc*T;
SYCUR();
gbfj(i);
}
}

void
trm()/*terminate the program.*/
{
free(bf.a);
tcsetattr(0,TCSANOW,&otos);
}

void
updm()/*update mode indicator.*/
{
write(1,MVTL,6);
WRVID((mod?"1":"0"),1);
}

void
upda()/*update all.*/
{
updm();
write(1,MVR,3);
if(!iso)updfnm();
gbfdpla();
}

void
sv(){/*save file (only if not in isolated mode).*/
DP("SAVE!\n");
int fd;
char wbf[RWBFSZ];
int csz;
int ri;
int rl;
fd=open("sav",O_WRONLY,O_TRUNC);
EE(fd<0,cant open file for save.\n,25)
/*TODO: need a loop here.*/
csz=bf.gst>RWBFSZ?RWBFSZ:bf.gst;
memcpy(&wbf,bf.a,bf.gst);
write(fd,&wbf,bf.gst);
ri=bf.gst+bf.gsz;
rl=bf.sz-ri;
memcpy(&wbf,bf.a+ri,rl);
write(fd,&wbf,rl);
EE(close(fd)<0,cant save file.\n,17)
updfnmtch(0);/*reset buffer touched state.*/
}

/*clear stderr file.
FOR DEBUG ONLY.*/
void
clerr(){write(2,ERSA,4);write(2,MVTL,6);}

int
main(int argc,char** argv){/*main func. involves main loop.*/
struct termios tos;
int fd;
ssize_t rb;
char rbf[RWBFSZ];
unsigned char c;
wsz=(struct winsize){0};
bf=(gbf){0};
bfl=0;
mod=0;
iso=0;
tch=0;
row=2;
col=1;
E(argc>2,too many args.\n,15)
/*if not target file is specified,then enter isolated mode.
since is this case empty buffer is going to be created,it'll be
nice to set mod to 1(insert) by default.*/
if(argc==1){
iso=1;
mod=1;
}
/*enter raw terminal mode.*/
tcgetattr(0,&tos);
otos=tos;
/*atexit returns 0 if successfull.*/
EE(atexit(&trm),cant set an exit function.\n,27)
tos.c_lflag&=~(ECHO|ECHONL|ICANON|ISIG);
tos.c_iflag&=~(IXON|ICRNL);
tos.c_oflag&=~OPOST;/*prevent terminal from treating \n as \n\r.*/
tos.c_cc[VMIN]=1;
tos.c_cc[VTIME]=0;
tcsetattr(0,TCSANOW,&tos);
/*finish entering raw mode here.*/
write(1,ERSA,4);
E(ioctl(1,TIOCGWINSZ,&wsz)<0,cant get winsize.\n,18)
gbfini();
if(argc==2)
{
	int j,si;/*fph iterator, slash idx.*/
	fph=argv[1];
	fd=open(fph,O_RDWR);
	fnm=fph;
	j=0;
	si=-1;
	/*find last slash(/) idx.*/
	while(fph[j]!=0){if(fph[j]==47)si=j;++j;}
	DP("si:%d,j:%d\n",si,j);
	fnml=j-si-1;/*exclude null-byte.*/
	AE(fnm,fnml+1,main,8)/*include null-byte.*/
	j=si;
	while(++j<=si+fnml+1)fnm[j-si-1]=fph[j];
	E(fd<0,cant open target.\n,18)
	while((rb=read(fd,&rbf,RWBFSZ))>0)
	{
	R(bf.a,bf.a,bf.sz+rb,main,8)
	memcpy(bf.a+bf.sz,&rbf,rb);
	bf.sz+=rb;
	}
	E(rb<0,cant read target.\n,18)
	E(close(fd)<0,cant close target.\n,19)
}
upda();
SYCUR();
while(1){
	if(read(0,&c,1)>0){
		/*Enter key generates 13(CR) char,
		but we want to make it 10(NL).*/
		if(c==13)c=10;
		switch(c){
		/*ctrl+q.*/
		case CTR(113):return 0;
		/*alt+j can't be detected so easily that's why
		I decided to remap ALT+j key sequence into CTRL+\
		(which produces code 28). so treat this fancy 28 as
		ALT+j actually.*/
		case 28:{
			mod^=1;
			updm();
			SYCUR();
			break;
		}
		/*ctrl+s.*/
		case CTR(115):{if(!iso)sv();break;}
		/*\. CLEAR stderr fiel.FOR DEBUG ONLY.*/
		AC(92,clerr)
		/*]. print buffer. FOR DEBUG ONLY.*/
		AC(93,pbuf)
		/*j.*/
		AC(106,gbfb)
		/*;.*/
		AC(59,gbff)
		/*l.*/
		AC(108,gbfd)
		/*k.*/
		AC(107,gbfu)
		/*a.*/
		AC(97,gbfls)
		/*d.*/
		AC(100,gbfle)
		/*s.*/
		AC(115,gbfdb)
		/*f.*/
		AC(102,gbfdf)
		/*e.*/
		AC(101,gbfel)
		/*h.*/
		AC(104,gbfsd)
		/*u.*/
		AC(117,gbfsu)
		/*backspace.*/
		case 8:case 127:{if(mod)gbfdb();break;}
		default:{
		/*if we're not in insert mode or we try
		to insert a non-alphabet character(\n(10) exclusive)
		just do nothing.*/
		if(mod!=1||((c<32||c>126)&&c!=10&&c!=9))break;
		pc:/*print char label.*/
		gbfinsc(c);
		if(c==10){/*handle \n case separately.*/
			write(1,ERSF,3);
			/*move cursor to the begining of
			next row.*/
			++row;
			col=1;
			SYCUR();
			gbfdplrst();
		}else if(c==9){/*handle \t char.*/
		write(1,ERSLF,3);
		col+=T;
		SYCUR();
		gbfdplrstl();
		}else{/*printable characters.*/
			write(1,&c,1);
			++col;
			/*we don't need to call SYCUR here,
			because writing a char into stdin had aldready
			moved cursor one position right.*/
			gbfdplrstl();
		}
		updfnmtch(1);/*say that buffer is modified now.*/
		}
		}
	}
}
}