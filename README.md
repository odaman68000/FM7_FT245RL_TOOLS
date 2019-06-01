FM-7 FT245RL Tools
-----

## はじめに

秋月電子から発売されている、FT245RLを利用した 8bit - USB変換ボードを活用した FM-7 向けツール群です。  
このツールを使うと、以下のようなことが出来ます。

* `BINSEND` : PCとFM-7の間でバイナリデータの送受信
* `BUBEMUL` : FM-7上から、PC上のフォルダをディスクドライブとして見立てて利用
* `D77SEND` : PCからD77ディスクイメージを転送してFM-7上で実FDDメディア上に書き込み

FT245RLを使っている為、約16KB/secという非常に高速な通信速度で上記の事が行えます。FM-7からは、かつてないほどの高速デバイスとして扱え、PC上で開発したバイナリの転送や、FM-7上の資産をPCへ転送したり、様々な利用が可能です。

## FT245RL を FM-7 に接続するには？

回路図が同梱してあります。

* [回路図](https://github.com/odaman68000/FM7_FT245RL_TOOLS/blob/master/FM7用FT245回路図201905.png)
* [実体配線図](https://github.com/odaman68000/FM7_FT245RL_TOOLS/blob/master/FM7用FT245実体配線図201905.png)

今となっては FM-7 向けの 32ピンコネクタの入手が少々難しいですが、上記回路図を実装する事で FM-7 と FT245RL を接続できます。

## 使い方

##### PC(Linux)/Mac で利用可能なコマンド
* `ft245tools` - makeコマンドでビルドします

基本的には、パラメタを全く指定せずにコマンドを起動するとヘルプが表示されるので、数多くの機能の使い方が判るようになっています。

##### FM-7 側で利用するコマンド

* `FT245TRN` - バイナリデータ送受信

まずは何とかこのコマンドをFM-7側に転送してください。鶏と卵じゃありませんが、どうしてもFM-7側に受信用プログラムとしてコレが必要です。

このデータは、$FC00 〜 $FC63 に配置します。その上で、 `SAVEM "FT245TRN",&HFC00,&HFC63,&HFC06` としてテープもしくは FD に保存してください。

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

## 謝辞・作者について

FT245RL を FM-7 に接続するアイデアおよび、ハードウェア設計は shuji_akita2001 氏 ( shuji_akita2001@yahoo.co.jp ) によります。たまたま自分が、 shuji_akita2001 氏よりこのハードウェアを紹介いただいた事で、このツール群を実装するに至りました。  
元々は、プリンタポートを使った 1,200bps 程度の低速通信で頑張ってたところ、16KB/secもの超高速でデータの送受信できるこのハードウェアに出会い、何とかコレを実用レベルで使えるように仕立て上げるべし！と強く思い、一気に作り上げました。  
このハードウェアが無ければ今だに、FDイメージを延々1時間掛けてFM-7に転送していた事でしょう。今は5分も掛かりませんよｗｗｗ

Software : Copyright (C) 2019 by odaman68000 (odaman68k.amiga@gmail.com)  
Hardware : Copyright (C) 2019 by shuji_akita2001 (shuji_akita2001@yahoo.co.jp)

