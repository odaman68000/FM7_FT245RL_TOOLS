all : bin/DRVEMUL bin/BUBEMUL bin/FT245TRN bin/FT245D77

bin/DRVEMUL : bin DRVEMUL
	cp -p DRVEMUL bin

bin/BUBEMUL : bin BUBEMUL
	cp -p BUBEMUL bin

bin/FT245TRN : bin FT245TRN
	cp -p FT245TRN bin

bin/FT245D77 : bin FT245D77
	cp -p FT245D77 bin

bin :
	mkdir bin

DRVEMUL : drvemul.6809 DBODY
	asm6809 -o DRVEMUL -l drvemul.lst drvemul.6809

DBODY : dbody.6809
	asm6809 -o DBODY -l dbody.lst dbody.6809

BUBEMUL : ft245bub.6809 FT245TRN
	asm6809 -o BUBEMUL -l ft245bub.lst ft245bub.6809

FT245TRN : ft245trn.6809
	asm6809 -o FT245TRN -l ft245trn.lst ft245trn.6809

FT245D77 : ft245d77.6809
	asm6809 -o FT245D77 -l ft245d77.lst ft245d77.6809
