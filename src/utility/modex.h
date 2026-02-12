
#ifndef GOT_GFXCALL
#define GOT_GFXCALL
#endif

void GOT_GFXCALL xsetmode(void);
void GOT_GFXCALL xshowpage(unsigned page);
void GOT_GFXCALL xfput(int x,int y,unsigned int pagebase,char far *buff);
void xcopyd2dmasked(int SourceStartX,
     int SourceStartY, int SourceEndX, int SourceEndY,
     int DestStartX, int DestStartY, MaskedImage * Source,
     unsigned int DestPageBase, int DestBitmapWidth);
void xcopyd2dmasked2(
     int SourceEndX, int SourceEndY,
     int DestStartX, int DestStartY, MaskedImage * Source,
     unsigned int DestPageBase);
void GOT_GFXCALL xcopys2d(int SourceStartX, int SourceStartY,
     int SourceEndX, int SourceEndY, int DestStartX,
     int DestStartY, char* SourcePtr, unsigned int DestPageBase,
     int SourceBitmapWidth, int DestBitmapWidth);
void GOT_GFXCALL xcopyd2d(int SourceStartX, int SourceStartY,
     int SourceEndX, int SourceEndY, int DestStartX,
     int DestStartY, unsigned int SourcePageBase,
     unsigned int DestPageBase, int SourceBitmapWidth,
     int DestBitmapWidth);
void xline(int x0,int y0,int x1,int y1,int color,int page);
unsigned int xcreatmaskimage(MaskedImage * ImageToSet,
   unsigned int DispMemStart, char * Image, int ImageWidth,
   int ImageHeight, char * Mask);
void GOT_GFXCALL xpset(int X, int Y, unsigned int PageBase, int Color);
void GOT_GFXCALL xget(int x1,int y1,int x2,int y2,unsigned int pagebase,
          char *buff,int invis);
void GOT_GFXCALL xput(int x,int y,unsigned int pagebase,char *buff);
void GOT_GFXCALL xtext(int x, int y, unsigned int pagebase, char far* buff, int color);
void GOT_GFXCALL xfillrectangle(int StartX, int StartY, int EndX, int EndY,
                    unsigned int PageBase, int Color);

unsigned int xcreatemaskimage(MaskedImage * ImageToSet,
     unsigned int DispMemStart, char * Image, int ImageWidth,
     int ImageHeight, char * Mask);

unsigned int xcreatmaskimage2(MaskedImage * ImageToSet,
     unsigned int DispMemStart, char * Image, int ImageWidth,
     int ImageHeight, char * Mask);

void xcopyd2dmasked(int SourceStartX,
     int SourceStartY, int SourceEndX, int SourceEndY,
     int DestStartX, int DestStartY, MaskedImage * Source,
     unsigned int DestPageBase, int DestBitmapWidth);
unsigned int GOT_GFXCALL xpoint(int X, int Y, unsigned int PageBase);
