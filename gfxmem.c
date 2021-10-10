/*
 *  gfxmem.c  - graphically display available memory.  Quick program
 *     that's a freebie.  Give it to anyone you please, just don't sell it.
 *
 *       Version 0.0 - initial version, released on INFO-AMIGA.
 *       Version 0.1 - added numeric indication of available and used memory
 *                     in bar graphs.
 *       Version 0.2 - added condition compilation for timer.device code.  If
 *                     I can figure out how to make it create timer signals,
 *                     then I can define USE_TIMER for much more prompt
 *                     response to window sizing attempts.  Oh well.
 *       Version 0.3 - removed conditional compilation for timer.device
 *                     code.  We now use the timer, and all seems well.
 *       Version 0.4 - calculate character data placement according to
 *                     display preferences.  Check for updated preferences
 *                     while running.
 *	 Version 0.5 - don't display fast memory if none exists.  Handle
 *		       Lattice C 3.03 in V1.1 update kit that insists on
 *		       creating a window for stdin, stdout and stderr with-
 *		       out asking.  Foo!
 *
 *  TODO:
 *       Add menu selection package to display Kbytes or percent of memory
 *       used and free.
 *
 *       Add requestor to display statistics like min and max for free and
 *       avail, time running, etc..
 *
 *
 *                 Copyright (C) 1985,
 *                 Louis A. Mamakos
 *                 Software & Stuff
 */
static char copyright[] = 
 "Copyright (C) 1985, 1986\r\nLouis A. Mamakos\r\nSoftware & Stuff";

#define  PROG_VER "GfxMem 0.5 "

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/exec.h>
#include <exec/execbase.h>
#include <exec/ports.h>
#include <exec/devices.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <hardware/blit.h>
#include <graphics/copper.h>
#include <graphics/regions.h>
#include <graphics/rastport.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <graphics/gels.h>
#include <intuition/intuition.h>
#include <stdio.h>

extern struct ExecBase *SysBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct MsgPort *timerport, *CreatePort();
struct timerequest timereq;

struct NewWindow NewWindow = {
   10, 10, 450, 50,           /* sizes */
   -1, -1,                    /* pens */
   CLOSEWINDOW | ACTIVEWINDOW | SIZEVERIFY |
                 NEWSIZE | NEWPREFS | REFRESHWINDOW,   /* IDCMP flags */
   WINDOWDRAG | WINDOWDEPTH | WINDOWCLOSE | WINDOWSIZING | SIMPLE_REFRESH,
   NULL, NULL,                /* gadget, checkmark */
   PROG_VER,                  /* title */
   NULL, NULL,                /* screen, bitmap */
   100, 30, 640, 100,         /* min and max sizing */
   WBENCHSCREEN               /* on the workbench screen */
};

ULONG    AvailMem ();
ULONG    ChipMax, FastMax, ChipFree, FastFree;
ULONG    ChipLargest, FastLargest;
int      dont_draw, char_size;

/*
 *  maxsize -- determine the total maximum size for all regions
 *   of the given type.  This code must be executed while
 *   FORBIDDEN (it accesses shared system structures).
 */
ULONG
maxsize (t)
    unsigned long t;
{
    /* THIS CODE MUST ALWAYS BE CALLED WHILE FORBIDDEN */
    ULONG size = 0;
    struct MemHeader *mem;
    struct ExecBase *eb = SysBase;

    for (mem = (struct MemHeader *) eb->MemList.lh_Head;
                        mem->mh_Node.ln_Succ; mem = mem->mh_Node.ln_Succ)
       if (mem -> mh_Attributes & t)
          size += ((ULONG) mem->mh_Upper - (ULONG) mem->mh_Lower);
    return size;
}

getsizes()
{
   if (ChipMax == 0) {  /* only do this once */
      Forbid ();
      ChipMax = maxsize (MEMF_CHIP);
      FastMax = maxsize (MEMF_FAST);
      Permit();
   }
   ChipFree = AvailMem (MEMF_CHIP);
   ChipLargest = AvailMem (MEMF_CHIP | MEMF_LARGEST);
   FastFree = AvailMem (MEMF_FAST);
   FastLargest = AvailMem (MEMF_FAST | MEMF_LARGEST);
}

starttimer()
{
   timereq.tr_time.tv_secs = 1;
   timereq.tr_time.tv_micro = 0;
   timereq.tr_node.io_Command = TR_ADDREQUEST;
   timereq.tr_node.io_Flags = 0;
   timereq.tr_node.io_Error = 0;
   timereq.tr_node.io_Message.mn_ReplyPort = timerport;
   SendIO((char *) &timereq.tr_node);
}

/*
 *  This function is called during startup and when new preferences are
 *  selected.  Get the font size from the Preferences structure.
 */
newprefs()
{
   char FontHeight;

   GetPrefs(&FontHeight, sizeof (FontHeight));
   switch (FontHeight) {
   case TOPAZ_SIXTY:
      char_size = 11;
      break;

   case TOPAZ_EIGHTY:
      char_size = 8;
      break;

   default:
      char_size = 12;
   }
}


/*
 *  Main function.  Call intution to create a new window for us on the
 *  screen.  Go from there.
 *
 * We no longer have our main() function called main();  instead we
 * use _main().  This keeps the standard _main.c function from Lattice
 * from creating a window for us that's not going to get used, and will
 * just clutter things. They figure that they are going to help us out, and
 * create a CON: type window for us.  This is ugly, and of no use to
 * us.  Gak!  Why don't they worry about their stupid compiler 
 * generating good (or even correct!) code with out this feeping
 * creaturism.  Flame off. 
 */
_main(args)
   char *args;
{
   struct Window *w;
   struct IntuiMessage *msg, *GetMsg();
   int waitmask;

   timerport = NULL;
   IntuitionBase = (struct IntuitionBase *)
                   OpenLibrary("intuition.library", 0);
   if (IntuitionBase == NULL)
      _exit(1);
   GfxBase = (struct GfxBase *) OpenLibrary("graphics.library", 0);
   if (GfxBase == NULL) {
      CloseLibrary(IntuitionBase);
      _exit(2);
   }

   getsizes();
   if (FastMax == 0)			/* if no fast memory in system */
	NewWindow.Height = 30;		/* make window smaller */

   if ((w = (struct Window *) OpenWindow(&NewWindow)) == NULL) {
      CloseLibrary(GfxBase);
      CloseLibrary(IntuitionBase);
      _exit(3);
   }
   if ((timerport = CreatePort("Timer Port", 0)) == NULL) {
      CloseWindow(w);
      CloseLibrary(GfxBase);
      CloseLibrary(IntuitionBase);
      _exit(5);
   }
   if (OpenDevice(TIMERNAME, UNIT_VBLANK, (char *) &timereq, 0) != 0) {
      DeletePort(timerport);
      CloseWindow(w);
      CloseLibrary(GfxBase);
      CloseLibrary(IntuitionBase);
      _exit(4);
   }

   newprefs();
   redraw(w, TRUE);
   starttimer();
   waitmask = (1 << w->UserPort->mp_SigBit) |
              (1 << timerport->mp_SigBit);
   for(;;) {
      Wait(waitmask);
      while (msg = GetMsg(w->UserPort)) {
         switch (msg->Class) {
         case CLOSEWINDOW:
            ReplyMsg(msg);
            AbortIO(&timereq);
            CloseDevice(&timereq);
            DeletePort(timerport);
            CloseWindow(w);
            CloseLibrary(GfxBase);
            CloseLibrary(IntuitionBase);
            _exit(0);

         case REFRESHWINDOW:
            BeginRefresh(w);
            dont_draw = 0;
            redraw(w, TRUE);
            EndRefresh(w, TRUE);

         case ACTIVEWINDOW:
            /* when window is active, display version */
            SetWindowTitles(w, -1, 
			    "Graphical memory display by Louis A. Mamakos");
            break;

         case NEWSIZE:
            dont_draw = 0;
            redraw(w, TRUE);
            break;

         case SIZEVERIFY:
            dont_draw = 1;
            break;

         case NEWPREFS:
            newprefs();
            redraw(w, TRUE);
            break;
         }
         ReplyMsg(msg);
      } /* while */

      if (GetMsg(timerport)) {
         redraw(w, FALSE);
         starttimer();
      }
   } /* for */
}


#define TOP w->BorderTop
#define BOTTOM w->Height - w->BorderBottom
#define LEFT w->BorderLeft
#define RIGHT w->Width - w->BorderRight
#define GUTTER 3        /* pixels of veritical spacing between bars */

/*
 *  Redraw all of the stuff in the window.
 */
redraw(w, refresh)
   struct Window *w;
   int refresh;
{
   register struct RastPort *rp = w->RPort;
   register short x_min, y_min, x_max, y_max;
   static short AvailWidth, AvailHeight, HorizSpace, Thickness, Scale;
   static long old_chipfree, old_fastfree;
   char txt[10];

   if (dont_draw)
      return 0;
   getsizes();
   if (refresh) {
      SetAPen(rp, 2);
      SetBPen(rp, 2);
      SetOPen(rp, 2);
      RectFill(rp, LEFT, TOP, RIGHT, BOTTOM);
      /*  recalculate the spacing paramters for this sized window */
      AvailWidth = w->Width - w->BorderRight - w->BorderLeft;
      AvailHeight = w->Height - w->BorderTop - w->BorderBottom;
      HorizSpace = AvailWidth/20; /* use 5% of available space as margin */
      AvailWidth -= HorizSpace * 2;
      if (FastMax)
	Thickness = (AvailHeight - GUTTER*3) / 2;
      else
	Thickness = (AvailHeight - GUTTER*2);

      if (ChipMax > FastMax)
         Scale = ChipMax/AvailWidth;
      else
         Scale = FastMax/AvailWidth;
   } else
      if (old_chipfree == ChipFree && old_fastfree == FastFree)
         return 0;
   old_chipfree = ChipFree;
   old_fastfree = FastFree;
   SetAPen(rp, 3);
   SetOPen(rp, 1);
   SetBPen(rp, 2);
   x_min = HorizSpace;
   y_min = TOP + GUTTER;
   x_max = x_min + (ChipMax - ChipFree)/Scale;
   y_max = y_min + Thickness;
   RectFill(rp, x_min, y_min, x_max, y_max);
   if ((Thickness > char_size) && (x_max - x_min > 6 * char_size)) {
      sprintf(txt, "%4dK", (ChipMax - ChipFree) >> 10);
      SetAPen(rp, 1);
      SetBPen(rp, 3);
      Move(rp, x_max - 5*char_size - 6, y_min + Thickness/2 + 3);
      Text(rp, txt, 5);
   }
   x_min = x_max;
   x_max = x_min + ChipFree/Scale;
   SetAPen(rp, 0);
   RectFill(rp, x_min, y_min, x_max, y_max);
   if ((Thickness > char_size) && (x_max - x_min > 6 * char_size)) {
      sprintf(txt, "%4dK", ChipFree>>10);
      SetAPen(rp, 1);
      SetBPen(rp, 0);
      Move(rp,x_min + 5, y_min + Thickness/2 + 3);
      Text(rp, txt, 5);
   }
   if ((HorizSpace > char_size + 5) && Thickness > char_size + 1) {
      SetAPen(rp, 1);
      SetBPen(rp, 2);
      Move(rp, HorizSpace - char_size - 1, y_min + Thickness/2 + 4);
      Text(rp, "C", 1);
   }

   if (FastMax == 0)
	return;

   x_min = HorizSpace;
   x_max = x_min + (FastMax - FastFree)/Scale;
   y_min = y_max + GUTTER;
   y_max = y_min + Thickness;
   SetAPen(rp, 3);
   RectFill(rp, x_min, y_min, x_max, y_max);
   if ((Thickness > char_size) && (x_max - x_min > 6 * char_size)) {
      sprintf(txt, "%4dK", (FastMax - FastFree) >> 10);
      SetAPen(rp, 1);
      SetBPen(rp, 3);
      Move(rp, x_max - 5*char_size - 6, y_min + Thickness/2 + 3);
      Text(rp, txt, 5);
   }
   x_min = x_max;
   x_max = x_min + FastFree/Scale;
   SetAPen(rp, 0);
   RectFill(rp, x_min, y_min, x_max, y_max);
   if ((Thickness > char_size) && (x_max - x_min > 6 * char_size)) {
      sprintf(txt, "%4dK", FastFree>>10);
      SetAPen(rp, 1);
      SetBPen(rp, 0);
      Move(rp,x_min + 5, y_min + Thickness/2 + 3);
      Text(rp, txt, 5);
   }
   if ((HorizSpace > char_size + 5) && Thickness > char_size + 1) {
      SetAPen(rp, 1);
      SetBPen(rp, 2);
      Move(rp, HorizSpace - char_size - 1, y_min + Thickness/2 + 3);
      Text(rp, "F", 1);
   }
}

