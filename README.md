FM-7 FT245RL Tools
-----

## はじめに

秋月電子から発売されている、FT245RL を利用した [USB-8bitパラレル変換ボード](http://akizukidenshi.com/catalog/g/gK-01799/)を活用した FM-7 向けツール群です。  
このツールを使うと、以下のようなことが出来ます。

* `BINSEND` : PCとFM-7の間でバイナリデータの送受信
* `BUBEMUL` : FM-7上から、PC上のフォルダをディスクドライブとして見立てて利用
* `D77SEND` : PCからD77ディスクイメージを転送してFM-7上で実FDDメディア上に書き込み
* `DRVEMUL` : FM-7側から PCに保存されている D77イメージを実ドライブとしてアクセス

FT245RLを使っている為、約16KB/secという非常に高速な通信速度で上記の事が行えます。FM-7からは、かつてないほどの高速デバイスとして扱え、PC上で開発したバイナリの転送や、FM-7上の資産をPCへ転送したり、様々な利用が可能です。

## FT245RL を FM-7 に接続するには？

回路図が同梱してあります。

* [回路図](https://github.com/odaman68000/FM7_FT245RL_TOOLS/blob/master/FM7用FT245回路図201905.png)
* [実体配線図](https://github.com/odaman68000/FM7_FT245RL_TOOLS/blob/master/FM7用FT245実体配線図201905.png)

今となっては FM-7 向けの 32ピンコネクタの入手が少々難しいですが、上記回路図を実装する事で FM-7 と FT245RL を接続できます。

[USB-8bitパラレル変換ボード](http://akizukidenshi.com/catalog/g/gK-01799/)のジャンパーは、以下のように設定して使用します。
* J1 → 2-3間をショート (VCCからの供給)
* J2 → オープン (VCCに外部から電源を供給する)

##### 注意事項

実態配線図にある SV2 端子は、単純に +5V, GND であり、短絡してはいけません。抵抗を挟んで LED を搭載するとか、そのような用途に使ってください。

## 使い方

##### PC(Linux)/Mac で利用可能なコマンド
* `ft245tools` - makeコマンドでビルドします

基本的には、パラメタを全く指定せずにコマンドを起動するとヘルプが表示されるので、数多くの機能の使い方が判るようになっています。

##### FM-7 側で利用するコマンド

* `FT245TRN` - バイナリデータ送受信

まずは何とかこのコマンドをFM-7側に転送してください。鶏と卵じゃありませんが、どうしてもFM-7側に受信用プログラムとしてコレが必要です。  
最後に BASIC による転送プログラムを掲載しておきますのでそちらも参考に。  

このデータは、$FC00 〜 $FC63 に配置します。その上で、`SAVEM "FT245TRN",&HFC00,&HFC63,&HFC06` としてテープもしくは FD に保存してください。  

`LOADM "FT245TRN",,R` として起動した上で、PC(Linux)/Mac 側から以下のコマンドを実行する事で、FM-7 側へデータを転送できます。

```
# ft245tools binsend [Filename] [配置先アドレス]
```
 
* BUBEMUL - PC(Linux)/Mac のディレクトリをドライブと見立てて動作

まずは最初に、`FT245TRN` を利用して、FM-7側へ転送してください。配置アドレスは $6809〜 です。  
`EXEC &H6809` とする事で、拡張 BASIC がインストールされます。以下のコマンドが利用可能です。

* `BUBR FILES` : ファイル一覧の表示
* `BUBR LOAD "Filename"` : アスキーセーブされた BASIC プログラムの読込み (Experimental)
* `BUBR SAVEM "Filename",&Hssss,&Heeee,&Hxxxx` : バイナリセーブ
* `BUBR LOADM "Filename"[,[&Hoooo][,R]]` : バイナリロード

PC(Linux) / Mac 側では以下のようにしてサーバプログラムを起動します。`Directory name` として指定したディレクトリが、上記のコマンドで FM-7 側からアクセス可能となります。

```
# ft245tools bubemul [Directory name]
```

バイナリデータの保存形態は、FM-7の F-BASIC に準拠しており、バイナリデータの前後にヘッダとフッタが付与されて保存されます。

* DRVEMUL - PC(Linux)/Mac 上に保存されている D77 を、FM-7から実ドライブとしてアクセス

まずは最初に、`FT245TRN` を利用して、`DRVEMUL`をFM-7側へ転送してください。配置アドレスは $6809〜 です。  
`EXEC &H6809` とする事で、ドライブアクセス関係の BIOS が拡張されます。  

その後、PC(Linux)/Mac 側で以下のようにコマンド起動します。

```
# ft245tools d77emul [.D77 filename]
```

これ以降は ドライブ 0: に対する全てのアクセスが、PC(Linux)/Mac上の D77 へとリダイレクトされます。  
ただ現状は Experimental という事で、FM-7側からいくらセクター情報を書き換えても PC(Linux)/Mac上でメモリ上記憶するだけで、D77ファイル自体の書き換えや更新は行いません。

## 謝辞・作者について

FT245RL を FM-7 に接続するアイデアおよび、ハードウェア設計は shuji_akita2001 氏 ( shuji_akita2001@yahoo.co.jp ) によります。たまたま自分が、 shuji_akita2001 氏よりこのハードウェアを紹介いただいた事で、このツール群を実装するに至りました。  
元々は、プリンタポートを使った 1,200bps 程度の低速通信で頑張ってたところ、16KB/secもの超高速でデータの送受信できるこのハードウェアに出会い、何とかコレを実用レベルで使えるように仕立て上げるべし！と強く思い、一気に作り上げました。  
このハードウェアが無ければ今だに、FDイメージを延々1時間掛けてFM-7に転送していた事でしょう。今は5分も掛かりませんよｗｗｗ

Software : Copyright (C) 2019 by odaman68000 (odaman68k.amiga@gmail.com)  
Hardware : Copyright (C) 2019 by shuji_akita2001 (shuji_akita2001@yahoo.co.jp)

-----
#### 付録：BASICで`FT245TRN`を転送する方法

`FT245TRN`は100バイトのバイナリですが、これをFM-7側に入力するの大変だなぁ...って人には、いい方法があります。(100バイト入力とドッチもドッチかも?)  

1. 以下のBASICリストを FM-7側で入力し、念のため保存
1. FM-7に搭載したFT245RLとPC(Linux)/MacをUSBケーブルで接続したのち、BASICプログラムを`RUN`します
1. PC(Linux)/Mac 側からは以下のようにしてコマンド起動します。  
    `# ft245tools binsend FM-7/bin/FT245TRN fc00`
1. 成功したら、FM-7側で`FT245TRN`が保存されます。  
    BASICリストを見てもらえれば判りますが、突然保存を開始しますので、テープもしくはFDの準備をしてから実行してください。

で、BASICプログラムは以下の通りです。

```
10 CLEAR ,&H5FFF:DEFINT A-Z:TBL=&H6000
20 DEFFNA(H,L)=(H+(H>=128)*256)*256+L
30 FOR I=0 TO 5:GOSUB 80:POKE TBL+I,DT:NEXT
40 ST=FNA(PEEK(TBL+0),PEEK(TBL+1))
50 ED=FNA(PEEK(TBL+2),PEEK(TBL+3))-1
60 FOR I=ST TO ED:GOSUB 80:POKE I,DT:NEXT
70 SAVEM "FT245TRN",ST,ED,ST+6:END
80 GOSUB 90:DT=D*2:GOSUB 90:DT=DT OR D:RETURN
90 IF PEEK(&HFDFE) AND &H80 THEN 90
100 POKE &HFDFD,1:D=PEEK(&HFDFE) AND &H7F:POKE &HFDFD,0:RETURN
```
