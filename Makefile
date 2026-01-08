CC=cl
CFLAGS=/O1 /GS- /W4 /DSECUREDESK /DFORCEDESK /DLOCK_MOUSE /DNOAUTOKILL /DNOTASKMGR /DNOKILL
LD=link
LDFLAGS=/entry:RawEntryPoint /subsystem:windows,5.1 /machine:x86 /release /merge:.rdata=.data /merge:.text=.data /filealign:512 /incremental:no /safeseh:no /fixed /opt:ref /opt:icf /ignore:4254
LIBS=kernel32.lib user32.lib gdi32.lib shlwapi.lib rpcrt4.lib advapi32.lib

bsod.exe: bsod.obj bsod.res
	$(LD) $** $(LDFLAGS) $(LIBS)

bsod.obj: bsod.c
	$(CC) /c $(CFLAGS) $**

bsod.res: bsod.rc
	$(RC) /fo$@ $**

clean:
	del bsod.obj ulldiv.obj bsod.res bsod.exe
