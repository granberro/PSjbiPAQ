    <li>  Install the bootloader.
    <p>
      Before continuing, be sure that the iPAQ is plugged into
      external power, and that the battery is charged, to protect against the
      small chance of power failure during the very limited period the iPAQ is
      reprogramming the bootloader flash.  Do <font color="red">NOT</font> 
      touch the power button or reset button on your iPAQ until you have
      performed the "Verify" step below.
    </p>

    <p>
      From the "Flash" menu on BootBlaster, select "Program".
      <br><img src="bootblaster-flashmenu.jpg"><br>
      A file dialog will open allowing you to select the bootloader to use.
      Select <code>bootldr.bin.gz</code>, which may have a version number embedded in it.  We use a gzip file because it has an internal checksum.
    </p>

    <p>
      <em>Wait patiently</em>.  It takes about 15 seconds to program
      the bootloader.  Do
      not interrupt this process, or the iPAQ may be left in an
      unusable state.
    </p>
    
    <p>
      From the "Flash" menu on BootBlaster, select "Verify".  
      <ol> <li>If it does <u>not</u> say that you have a valid bootloader, do
      <em>NOT</em> reset your iPAQ, do <em>NOT</em> turn off your iPAQ.
      <li>Instead, try programming the
      flash again.  
      <li>If that doesn't work, program your flash with your
      saved bootloader.  
      <li>If that doesn't work, send e-mail to
      bootldr@handhelds.org and/or get on the IRC and ask for help.
      Leave the iPAQ plugged in and do <em>NOT</em> reset it or turn it off.
      </ol>
    </p>
    <p>If everything has gone well, you have successfully
    installed the CRL bootldr program, which can run either Linux or PocketPC.  As yet,
    your PocketPC image is intact and should restart normally; the next step actually installs 
    Linux (overwriting Pocket PC).</p>
