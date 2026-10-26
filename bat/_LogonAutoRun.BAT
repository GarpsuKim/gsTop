Rem  LogonAutoRun.BAT

@echo off

start "alert" /d "C:\tools\booting_Alert" cmd /c "timeout /t 30 /nobreak >nul & booting_Alert_RUN.bat"

set "CURDIRx=%~dp0"
set "CURDIR=%CURDIRx:~0,-1%"

set "CHROME=C:\Program Files\Google\Chrome\Application\chrome.exe"

set  "GOOGLE_CALENDAR=https://calendar.google.com/calendar/u/1/r/agenda"

set "thisCalendar=file:///C:/Users/garpsu/Documents/@print/thisCalendar.pdf"

set "Week_ahead=file:///C:/Users/garpsu/Documents/@print/Week_ahead.pdf"

set "msedge=C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"

set "FireFox=C:\Program Files\Mozilla Firefox\firefox.exe"

set "MarketSummary=https://elwww.tistory.com/entry/%%E2%%97%%8F%%E2%%97%%8F%%E2%%97%%8B-%%EC%%95%%84%%EC%%B9%%A8-Market-Summary-7435811"

set "portfolio=https://www.investing.com/portfolio/?portfolioID=ZWY%%2BajZiMWo0Y21nZjMzMw%%3D%%3D"

set "sajeKongEe=https://t.me/ehdwl"

set "cnbcPolitics=https://www.cnbc.com/politics"

set "mostPopular=https://www.investing.com/news/most-popular-news"

set "Whale=C:\Program Files\Naver\Naver Whale\Application\whale.exe"

set "NaverCalendar=https://calendar.naver.com/main#%7B%22sSection%22%3A%22scheduleMain%22%2C%22oParameter%22%3A%7B%22sViewType%22%3A%22month%22%2C%22sDate%22%3A%222026-08-27%22%7D%7D"

set "SamsungFlow=C:\Program Files\WindowsApps\SAMSUNGELECTRONICSCoLtd.SamsungFlux_4.9.1902.0_x64__wyx1vj98g3asy\SamsungFlow.exe"

echo ===================================
echo 폴더 경로: %CURDIR%
echo ===================================

start "" "%SamsungFlow%"
start "" "C:\Windows\System32\UTCk.exe"
start "" "%Whale%"  "%NaverCalendar%"
start "" "%CHROME%"  "%GOOGLE_CALENDAR%"   "%thisCalendar%"  "%Week_ahead%"
start ""  "%msedge%"   "https://time.is/"  "https://www.weather.go.kr/w/index.do#dong/4128162200/37.6341824910845/126.832539910991/경기%20고양시덕양구%20화정동/SCH/화정역광장"

start ""  "C:\tools\wakemeonlan-x64\WakeMeOnLan.exe"
start ""  "C:\Program Files\Notepad++\notepad++.exe"

pause

:: ■ real time News
start ""  "%msedge%"  "https://www.wowtv.co.kr/"   "https://www.yna.co.kr/"  "https://kr.investing.com/news/latest-news"   "https://www.hankyung.com/"

:: financialjuice : 
:: finviz : 
start "" "%FireFox%"  "https://elite.finviz.com/news.ashx"  "%MarketSummary%" "%portfolio%" "%sajeKongEe%" "%mostPopular%"  "%cnbcPolitics%"

start ""  C:\Users\garpsu\AppData\Local\Programs\Opera\opera.exe   "https://www.financialjuice.com/home" 

pause

start ""  "%FireFox%"  "https://stockcharts.com/freecharts/candleglance.html?$INDU,$COMPQ,$SPX,EWY,$USD,$TNX,$WTIC,$GOLD,$USDJPY,MU,NVDA,$sox"  "https://stockcharts.com/freecharts/candleglance.html?$compq,SKHY,EWY,$SML,$SOX,MU,nvda,sndk,stx,wdc"  "https://stockcharts.com/freecharts/candleglance.html?$USD,$UST1M,$UST3M,$UST1Y,$UST2Y,$UST3Y,$UST5Y,$TNX,$UST20Y,$UST30Y"   "https://stockcharts.com/freecharts/candleglance.html?$USD,$GOLD,$COPPER,$WTIC,$NATGAS,$GBPUSD,$EURUSD,$USDJPY,$CHFUSD"   "https://stockcharts.com/freecharts/candleglance.html?$WTIC,$BRENT,$NATGAS,$COPPER,$GOLD,$SILVER,$SOYB,$CORN,$WHEAT,$CRB"  "https://stockcharts.com/freecharts/candleglance.html?$TWII,$NIKK,$HSI,$HSCEI,$SSEC,$N100,$CAC,$DAX,$BVSP,$MXX,$TSX"   "https://stockcharts.com/freecharts/candleglance.html?$USD,$GOLD,$TNX,$XDN,$xds"   "https://stockcharts.com/freecharts/candleglance.html?NVDA,AAPL,MSFT,GOOGL,AMZN,META,TSLA,AVGO,ORCL,NFLX,$SOX,MU"  "https://stockcharts.com/freecharts/candleglance.html?PLTR,HPE,DELL,IBM,CSCO,SMCI,INTC,AMD,TXN,QCOM,"  "https://stockcharts.com/freecharts/candleglance.html?EWY,$COMPQ,$SOX,MU,NVDA,AVGO,INTC,AMD,TSM,asml,lrcx,amat"  "https://stockcharts.com/freecharts/candleglance.html?MU,SNDK,STX,WDC,ORCL,$SOX,$COMPQ,EWY"  "https://stockcharts.com/freecharts/candleglance.html?BAC,C,WFC,PYPL,SCHW,JPM,TFC,USB,MS,KEY,COF,SYF,V,BRKb,KKR,BK,BX,PNC,GS,DFS,AXP,MA,STT,BLK,RY"          

start "" "C:\WMV\QV\wmtslogin.exe"
rem  start "" "C:\GnB HTS\GnBUpdater.exe"
start "" "C:\eFriend Plus x64\efriendplus\efriendplus.exe"

rem start ""  "C:\Program Files (x86)\TechSmith\Snagit 9\snagit32.exe"
rem start ""  "C:\Users\garpsu\AppData\Roaming\Telegram Desktop\Telegram.exe"

exit