
<!-- saved from url=(0105)ftp://ftp.gwdg.de/pub/linux/handhelds/distributions/familiar/releases/v0.8.4/install/bootblaster-save.inc -->
<html><head><meta http-equiv="Content-Type" content="text/html; charset=ISO-8859-1"></head><body><br><img src="ftp://ftp.gwdg.de/pub/linux/handhelds/distributions/familiar/releases/v0.8.4/install/bootblaster-screen.jpg"><br>
	 
<li>Save your PocketPC image for later restoration, if desired.  
    <ol>
    <li>
    <p>Execute "<code>Flash -&gt; Save Bootldr .gz
    Format</code>" in BootBlaster to save the bootloader in file "<code>\My
    Documents\saved_bootldr.gz</code>" on the iPAQ.
    <br><img src="ftp://ftp.gwdg.de/pub/linux/handhelds/distributions/familiar/releases/v0.8.4/install/bootblaster-bootldr-save.jpg"><br>
    Note that the Linux Bootloader will also boot PocketPC,
    so restoration of this file is not generally required.  Right at
    the moment, there is a bug in the Linux Bootloader which causes
    PocketPC to reinitialize itself every few boots.  You may indeed
    wish to keep and restore this bootloader if you restore PocketPC.</p>
    <br><img src="ftp://ftp.gwdg.de/pub/linux/handhelds/distributions/familiar/releases/v0.8.4/install/bootblaster-bootldr-saved.jpg">
    </li><li>
    <p>Execute "<code>Flash -&gt; Save Wince .gz
    Format</code>" in BootBlaster to save the PocketPC image in file
    "<code>\My Documents\wince_image.gz</code>" on the iPAQ.  This takes two to
    three minutes.</p>
    <br><img src="ftp://ftp.gwdg.de/pub/linux/handhelds/distributions/familiar/releases/v0.8.4/install/bootblaster-saved-wince.jpg">
    </li></ol>
    <p>
    If no backup of Pocket PC is desired, you can skip this step entirely.
    </p><p>
    Note that this procedure saves your bootloader and Pocket PC executable image: it does not preserve any
    data you may have entered in your iPAQ under Pocket PC.  So also synchronize your iPAQ to your host
    to preserve this data.  Note that Familiar does not *yet* have any way to resynchronize this data
    to Linux (we hope/expect to have Linux&lt;-&gt;Host synchronization in a near future release).
</p></li></body></html>