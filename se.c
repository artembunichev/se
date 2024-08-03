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
#define CTR(C) (C&0x1f)
#define BFISZ 2
/*buffer expand size.*/
#define BFXPNS 32
/*readwrite buf size*/
#define RWBFSZ 4096
/*write text in reverse video mode.S-str,L-len.*/
#define WRVID(S,L) (write(1,RVID,4),write(1,S,L),write(1,VRST,4))
/*sync terminal visual cursor with buffer cursor position.*/
#define SYCUR() scur(row,col)

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
,col;
/*editor mode. 0-navigate 1-edit.*/
char mod;
/*filename of currently edited file.*/
char* fnm;

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
(*s)[l]='\0';/*start building up a string from the end.so we start from null byte.*/
do{(*s)[l-i]=(x%10)+'0';/*convert digit to ascii char.*/
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
gbflsi(int l)/*idx of first char in line.*/
{
int n/*line count.*/
,i;
if(!bf.gst)i=bf.gst+bf.gsz;
n=0;
i=0;
while(n<l){
	if(i==bf.gst){i=bf.gst+bf.gsz;continue;}
	if(bf.a[i]=='\n')++n;
	++i;
}
dprintf(2,"line %d first char at %d\n",l,i);
}

void
gbfinsc(char c)/*insert char.*/
{
	/*if there is no enough space for gap-expand buf.*/
	if(!bf.gsz)gbfxpnd(bf.gsz+BFXPNS+1);
	bf.a[bf.gst]=c;/*put char before the gap.*/
	++bf.gst;--bf.gsz;/*move gap to the right(forward).*/
}

void
gbfinss(char* s)/*insert string.*/
{
	int sl/*lenght of inserted string.*/
	,i;
	sl=strl(s);
	/*if gap hasn't got enough space for string-expaind buf.*/
	if(bf.gsz<sl)gbfxpnd(sl-bf.gsz+BFXPNS);
	i=0;
	while(i<sl)bf.a[bf.gst+i]=s[i++];/*insert str chars one by one.*/
	bf.gst+=sl;bf.gsz-=sl;/*move gap to the right(forward).*/
}

/*disply rest of the buffer.*/
void gbfdplrst(){
int x;
x=bf.gst+bf.gsz;
write(1,bf.a+x,bf.sz-x);/*display everything what is after gap.*/
SYCUR();/*sync term cursor 'cause it's now in the end of the text.*/
}

/*display rest of the current line.*/
void gbfdplrstl(){
int u/*first aftergap idx.*/
,i;/*iterator and next \n or eof idx at the same time.*/
u=bf.gst+bf.gsz;
i=u;
while(i<bf.sz&&bf.a[i]!='\n')++i;/*find idx of next \n or eof.*/
write(1,bf.a+u,i-u);/*print all the chars between gap end and closes \n or eof.*/
SYCUR();/*sync term cur 'cause it's now in the end of line.*/
}

void
gbfdf()/*delete one char forward.*/
{
int agi;/*first aftergap idx.*/
agi=bf.gst+bf.gsz;
if(agi==bf.sz)return;/*we can't delete forward if cursor is in the end of text.*/
bf.gsz++;/*deleting forward is equal to shifting right gap boundary forward.*/
/*if we've deleted \n char then we need to move line below up and append it to the end. it requires us to
redraw all the text after gap. we do not need to change row/col vars 'cause cursor is still staying on the
same line.*/
if(bf.a[agi]=='\n'){write(1,ERSF,3);gbfdplrst();}
/*if char we've deleted is not a \n then we need to redraw only current line.*/
else{write(1,ERSLF,3);gbfdplrstl();}
}

void
gbfdb()/*delete one char backward.*/
{
int i;
if(!bf.gst)return;/*we can't delete backward if cursor in the begining of the text.*/
--bf.gst;++bf.gsz;/*deleting backward is equal to shifting left gap boundary backward.*/
/*if we've deleted \n char then we need to move current line above and append it to the end.*/
if(bf.a[bf.gst]=='\n'){
/*iterator and prev \n or ZERO idx. their semantic meanings are not the same.
actually if we're on the first text line i should points to -1 to have the
same meaning(idx before first char in line) in case when we're not on the
first line, but it's easier to organize loop for i to be 0 eventually.
so we'll just handle this case separately later.*/
i=bf.gst;
while(i>0&&bf.a[--i]!='\n');/*find prev \n or zero idx.*/
/*we do need to change row/col 'cause now cursor should be on the line above.*/
--row;/*go line above.*/
/*place cur in the end of previous line (bf.gst-i) is for prev line length
and +(!i) trick is for handling that semantic difference described above.*/
col=bf.gst-i+(!i);
SYCUR();/*sync term cursor with new row/col positions we've just set.*/
write(1,ERSF,3);/*erase everything after cursor.*/
gbfdplrst();/*reprint everything after cursor.*/
}
else{/*if char we've deleted is not a \n.*/
--col;/*move cursor back one char horizontally.*/
/*we don't necessary need to sync here (although we stll can) 'cause in
this case it's more simpler to move cursor left by appropriate esc-sequence.*/
write(1,MVL,3);/*so move cursor left.*/
/*as far as we didn't move any line, deletion operation affected only current
line, so we need to redraw only it.*/
write(1,ERSLF,3);
gbfdplrstl();
}
}

void
gbfel()/*erase the line cursor is currently on.*/
{
int i
,j;
/*TODO:to func.*/
i=bf.gst;
j=bf.gst+bf.gsz-1;
while(i>0&&bf.a[--i]!='\n');
i+=!!i;
/*TODO:to func.*/
while(++j<bf.sz-1&&bf.a[j]!='\n');j+=j==bf.sz-1;
bf.gst=i;bf.gsz=j-i;
col=1;SYCUR();write(1,ERSLA,4);
}

void
gbfelr()/*erase current line to the right.*/
{
int i;
/*TODO:to func.*/
i=bf.gst+bf.gsz-1;
while(++i<bf.sz-1&&bf.a[i]!='\n');i+=i==bf.sz-1;
bf.gsz=i-bf.gst;
write(1,ERSLF,4);
}

void
gbff()/*move cursor forward.*/
{
int nxi;
nxi=bf.gst+bf.gsz;
if(nxi==bf.sz)return;
bf.a[bf.gst]=bf.a[nxi];
if(bf.a[bf.gst]=='\n'){row++;col=1;SYCUR();}
else{col++;write(1,MVR,3);}
++bf.gst;
/*FOR DEBUG ONLY.*/
gbflsi(1);
}

void
gbfb()/*move cursor backward.*/
{
	int i;
	if(!bf.gst)return;
	bf.a[bf.gst+bf.gsz-1]=bf.a[bf.gst-1];
	bf.gst--;
	if(bf.a[bf.gst+bf.gsz]=='\n')
	{--row;
		i=bf.gst-1;
		while(i&&bf.a[i--]!='\n');
		col=bf.gst-i-(!!i);SYCUR();
	}else{--col;write(1,MVL,3);}
	/*FOR DEBUG ONLY.*/
	gbflsi(1);
}

void
gbfj(int j)/*jump to idx.*/
{
int e;
int d;
if(j<0||j>bf.sz||j==bf.gst||(j>bf.gst&&j<bf.gst+bf.gsz))return;
if(j<bf.gst)
{
	memcpy(bf.a+j+bf.gsz,bf.a+j,bf.gst-j);
	bf.gst=j;
}else{
	e=bf.gst+bf.gsz;
	d=j-e;
	memcpy(bf.a+bf.gst,bf.a+e,d);
	bf.gst+=d;
}
}

void
gbfd()/*move cursor down.*/
{
int i
,j;
i=bf.gst+bf.gsz;
j=1;
while(bf.a[i]!='\n'){if(i>bf.sz)return;++i;}
while((i+j)<bf.sz&&bf.a[i+j]!='\n'&&j<col)++j;
dprintf(2,"I:%d,bfsz:%d,J:%d\n",i,bf.sz,j);
gbfj(i+j);
++row;
if(j==col)write(1,MVD,3);
else{
col=j;
SYCUR();
}
}

void
gbfu()/*move cursor up.*/
{
int i
,j;
i=bf.gst;
j=i;
while(bf.a[--i]!='\n')if(i<0)return;
while(--j>=0&&bf.a[j]!='\n');
--row;
if(col>i-j){col=i-j;SYCUR();}else write(1,MVU,3);
gbfj(j+col);
}

void
gbfdpl()/*display buffer(display \n as \n\r without modifying original text).*/
{
int n
,i
,r
,j
,wl
,k
,z;
n=0;
i=0;
r=1;
k=0;
char* w;
write(1,MVBFST,6);
while(n<bfl&&i<bf.sz){
if(i==bf.gst){i=bf.gst+bf.gsz;continue;}
if(bf.a[i]=='\n')++n;
++i;
}
j=i-1;
dprintf(2,"2i: %d, j:%d\n",i,j);
while(r<wsz.ws_row&&++j<bf.sz)if(bf.a[j]=='\n')++r;
wl=j-i+r;
AE(w,wl,gbfdpl,7)
z=i;
while(z<j){
if(bf.a[z]=='\n'){
/*move term cursor to line start after every \n (mimic \r).*/
memcpy(w+k,"\n\r",2);
k+=2;
}
else{w[k]=bf.a[z];++k;}
++z;
}
dprintf(2,"2i: %d, n: %d, j:%d, r:%d,wl:%d,k:%d\n",i,n,j,r,wl,k);
memcpy(w+k,ERSF,3);
write(1,w,wl);
free(w);
SYCUR();
}

void
gbfsd()/*scroll screen down.*/
{
++bfl;gbfdpl();if(row==2){gbfd();--row;}else{gbfu();++row;}
dprintf(2,"gst now: %d\n",bf.gst);
}

void
gbfsu()/*scroll screen up.*/
{
if(!bfl)return;
--bfl;
gbfu();
gbfdpl();
}

void
trm()/*terminate program.*/
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
updfnm()/*update filename.*/
{
WRVID(fnm,strl(fnm));
}

void
upda()/*update all.*/
{
	updm();
	write(1,MVR,3);
	updfnm();
	gbfdpl();
}

void
sv()/*save file.*/
{
int fd;
char wbf[RWBFSZ];
int csz;
int ri;
int rl;
dprintf(2,"SAVING FILE.\n");
fd=open("sav",O_WRONLY,O_TRUNC);
EE(fd<0,cant open file for save.\n,25)
if(bf.gst>0){dprintf(2,"more 0\n");}
/*TODO: need a loop here.*/
csz=bf.gst>RWBFSZ?RWBFSZ:bf.gst;
memcpy(&wbf,bf.a,bf.gst);
write(fd,&wbf,bf.gst);
ri=bf.gst+bf.gsz;
rl=bf.sz-ri;
memcpy(&wbf,bf.a+ri,rl);
write(fd,&wbf,rl);
EE(close(fd)<0,cant save file.\n,17)
}

int
main(int argc,char** argv)/*main func. involves main loop.*/
{
	struct termios tos;
	int fd;
	ssize_t rb;
	char rbf[RWBFSZ];
	unsigned char c;
	wsz=(struct winsize){0};
	bf=(gbf){0};
	bfl=0;
	mod=0;
	row=2;
	col=1;
	E(argc>2,too many args.\n,15)
	/*FOR DEBUG ONLY.*/
	write(2,"",0);
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
		fnm=argv[1];
		fd=open(fnm,O_RDWR);
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
	gbflsi(1);
	while(1)
	{
		if(read(0,&c,1)>0)
		{
			switch(c){
			/*q.*/
			case CTR(113):return 0;
			/*j.*/
			case CTR(106):
			{
				mod^=1;
				updm();
				dprintf(2,"row:%d, col:%d\n",row,col);
				SYCUR();
				break;
			}
			/*s.*/
			case CTR(115):{sv();break;}
			/*j.*/
			AC(106,gbfb)
			/*;.*/
			AC(59,gbff)
			/*l.*/
			AC(108,gbfd)
			/*k.*/
			AC(107,gbfu)
			/*d.*/
			AC(100,gbfdf)
			/*s.*/
			AC(115,gbfdb)
			/*e.*/
			AC(101,gbfel)
			/*r.*/
			AC(114,gbfelr)
			/*h.*/
			AC(104,gbfsd)
			/*u.*/
			AC(117,gbfsu)
			case 8:case 127:{if(mod)gbfdb();break;}
			default:{
			if(mod!=1||c<32||c>126)break;
			pc:/*print char label.*/
			gbfinsc(c);
			write(1,&c,1);
			++col;
			gbfdplrstl();					
			}
			}
		}
	}
}