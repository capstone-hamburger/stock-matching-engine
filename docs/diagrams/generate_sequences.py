"""Render sequence diagrams with system Cairo; no Python packages required."""
from pathlib import Path
import ctypes as C
import zipfile

OUT = Path(__file__).resolve().parent
c = C.CDLL('libcairo.so.2')
fc = C.CDLL('libfontconfig.so.1')
fc.FcConfigGetCurrent.restype = C.c_void_p
fc.FcConfigAppFontAddFile.argtypes = [C.c_void_p, C.c_char_p]
for font in ['malgun.ttf', 'malgunbd.ttf']:
    path = Path('/mnt/c/Windows/Fonts') / font
    if path.exists():
        fc.FcConfigAppFontAddFile(fc.FcConfigGetCurrent(), str(path).encode())

def bind(name, args, ret=None):
    f = getattr(c, 'cairo_' + name)
    f.argtypes, f.restype = args, ret
    return f

P, D, I, S = C.c_void_p, C.c_double, C.c_int, C.c_char_p
surface_image = bind('image_surface_create', [I,I,I], P)
surface_svg = bind('svg_surface_create', [S,D,D], P)
create = bind('create', [P], P)
destroy = bind('destroy', [P])
finish = bind('surface_finish', [P])
surface_destroy = bind('surface_destroy', [P])
png = bind('surface_write_to_png', [P,S], I)
scale = bind('scale', [P,D,D])
rgb = bind('set_source_rgb', [P,D,D,D])
rectangle = bind('rectangle', [P,D,D,D,D])
fill = bind('fill', [P])
move = bind('move_to', [P,D,D])
line = bind('line_to', [P,D,D])
stroke = bind('stroke', [P])
width = bind('set_line_width', [P,D])
dash = bind('set_dash', [P,C.POINTER(D),I,D])
font_face = bind('select_font_face', [P,S,I,I])
font_size = bind('set_font_size', [P,D])
show = bind('show_text', [P,S])
class Ext(C.Structure):
    _fields_ = [(k,D) for k in ['xb','yb','w','h','xa','ya']]
extents = bind('text_extents', [P,S,C.POINTER(Ext)])

INK='#172b4d'; BLUE='#245bcc'; MUTED='#63758b'; LINE='#c5d1df'

class Diagram:
    def __init__(self, ctx, w, h, title, subtitle, actors, footer):
        self.ctx, self.w, self.h = ctx,w,h
        self.box(0,0,w,h,'#ffffff')
        self.box(0,0,w,10,BLUE)
        self.text(48,67,title,32,bold=True)
        self.text(48,106,subtitle,18,color=MUTED)
        self.x = [150+i*(w-300)/(len(actors)-1) for i in range(len(actors))]
        for x, (name, role) in zip(self.x,actors):
            self.path([(x,205),(x,h-105)],LINE,dashed=True)
            self.box(x-112,143,224,66,'#edf3ff')
            self.text(x,170,name,22,align='center',bold=True,color=BLUE)
            self.text(x,194,role,14,align='center',color=MUTED)
        self.y=250
        self.box(36,h-85,w-72,58,'#f2f5f9')
        self.text(54,h-50,footer,17,color=MUTED)
    def color(self,color):
        rgb(self.ctx,*[int(color[i:i+2],16)/255 for i in (1,3,5)])
    def box(self,x,y,w,h,color):
        self.color(color); rectangle(self.ctx,x,y,w,h); fill(self.ctx)
    def text(self,x,y,value,size=19,align='left',bold=False,color=INK):
        font_face(self.ctx,'Malgun Gothic'.encode(),0,int(bold)); font_size(self.ctx,size)
        e=Ext(); extents(self.ctx,value.encode(),C.byref(e))
        if align=='center': x-=e.xa/2
        if align=='right': x-=e.xa
        self.color(color); move(self.ctx,x,y); show(self.ctx,value.encode())
    def path(self,pts,color=BLUE,dashed=False):
        self.color(color); width(self.ctx,2)
        a=(D*2)(7,6); dash(self.ctx,a,2 if dashed else 0,0)
        move(self.ctx,*pts[0])
        for p in pts[1:]: line(self.ctx,*p)
        stroke(self.ctx); dash(self.ctx,a,0,0)
    def arrow(self,a,b,label,reply=False):
        x1,x2=self.x[a],self.x[b]; y=self.y
        if a==b:
            self.text(x1-15,y-9,label,18,align='right')
            self.path([(x1,y),(x1+42,y),(x1+42,y+25),(x1,y+25)],dashed=reply)
            tip=(x1,y+25); direction=-1
            self.y+=73
        else:
            self.text((x1+x2)/2,y-12,label,18,align='center')
            self.path([(x1,y),(x2,y)],dashed=reply)
            tip=(x2,y); direction=1 if x2>x1 else -1
            self.y+=65
        x,y=tip
        self.path([(x-direction*10,y-5),(x,y),(x-direction*10,y+5)])
    def band(self,label,warning=False):
        self.box(45,self.y-12,self.w-90,35,'#fff4e5' if warning else '#edf3ff')
        self.text(60,self.y+12,label,18,bold=True,color='#986111' if warning else BLUE)
        self.y+=61

def order(d):
    d.arrow(0,1,'BUY / SELL JSON')
    d.arrow(1,1,'JSON·필드·stockId 검증')
    d.band('alt  통신 형식 오류 또는 미등록 종목',True)
    d.arrow(1,0,'ERROR (code, message)',True)
    d.band('else  통신 검증 통과')
    d.arrow(1,2,'post: 엔진 작업 큐로 전달')
    d.arrow(2,3,'submit(OrderRequest)')
    d.arrow(3,3,'값·중복 주문 ID·처리 용량 검증')
    d.band('alt  엔진 검증 거절',True)
    d.arrow(3,2,'accepted=false / 거절 사유',True)
    d.band('else  주문 접수')
    d.arrow(3,3,'가격 우선 → 같은 가격은 FIFO')
    d.arrow(3,3,'체결 시 양쪽 주문·최근 체결가 갱신')
    d.arrow(3,3,'남은 수량은 호가창에 등록')
    d.arrow(3,2,'상태·체결량·잔량·trades[]',True)
    d.band('엔진 결과 공통 응답 경로 · 접수 성공 / 검증 거절')
    d.arrow(2,1,'결과 JSON → I/O 작업 큐',True)
    d.arrow(1,0,'ORDER_RESULT',True)

def cancel(d):
    d.arrow(0,1,'CANCEL_ORDER JSON')
    d.arrow(1,1,'필드·stockId 검증')
    d.band('alt  형식 오류 또는 미등록 종목',True)
    d.arrow(1,0,'ERROR (code, message)',True)
    d.band('else  통신 검증 통과')
    d.arrow(1,2,'post: 엔진 작업 큐로 전달')
    d.arrow(2,3,'cancel(orderId, userId)')
    d.arrow(3,3,'ID → 주문 존재 → 소유자 → 상태 검사')
    d.band('alt  취소 가능: Open / PartiallyFilled')
    d.arrow(3,3,'호가창에서 미체결 잔량 전체 제거')
    d.arrow(3,3,'상태 Canceled · 기존 체결량 유지')
    d.arrow(3,2,'success=true, canceledQuantity',True)
    d.band('else  주문 없음 / 타인 주문 / 이미 체결·취소 / 잘못된 ID',True)
    d.arrow(3,2,'success=false, failureReason',True)
    d.band('엔진 결과 공통 응답 경로')
    d.arrow(2,1,'취소 결과 JSON → I/O 작업 큐',True)
    d.arrow(1,0,'CANCEL_RESULT',True)

def price(d):
    d.arrow(0,1,'GET_CURRENT_PRICE(stockId)')
    d.arrow(1,1,'필드·stockId 검증')
    d.band('alt  형식 오류 또는 미등록 종목',True)
    d.arrow(1,0,'ERROR (code, message)',True)
    d.band('else  통신 검증 통과')
    d.arrow(1,2,'post: 엔진 작업 큐로 전달')
    d.arrow(2,3,'lastTradePrice()')
    d.band('alt  실제 체결 이력 있음')
    d.arrow(3,2,'가장 최근 체결 가격',True)
    d.band('else  최초 체결 전')
    d.arrow(3,2,'nullopt → JSON null',True)
    d.band('조회 결과 공통 응답 경로')
    d.arrow(2,1,'현재가 JSON → I/O 작업 큐',True)
    d.arrow(1,0,'CURRENT_PRICE(stockId, price)',True)

def service(d):
    d.band('통합 제안 · 외부 백엔드·DB·화면 갱신 단계는 이 저장소에 미구현',True)
    d.arrow(0,1,'매수·매도 주문 입력')
    d.arrow(1,2,'주문 요청')
    d.arrow(2,2,'인증·잔고·보유량 검증')
    d.arrow(2,3,'주문 ID 발급·기록 / 자산 예약')
    d.arrow(3,2,'저장·예약 결과',True)
    d.band('아래는 사전 검증·예약 성공 시의 대표 흐름')
    d.arrow(2,4,'WebSocket: BUY / SELL')
    d.arrow(4,4,'검증·매칭·주문/호가 갱신')
    d.arrow(4,2,'ORDER_RESULT (accepted, trades[])',True)
    d.band('alt  주문 접수 성공')
    d.arrow(2,3,'양쪽 주문 갱신·체결 저장·계좌 정산')
    d.arrow(3,2,'DB 반영 결과',True)
    d.band('else  명시적 주문 접수 거절',True)
    d.arrow(2,3,'거절 기록·예약 자산 해제')
    d.arrow(3,2,'DB 반영 결과',True)
    d.band('DB 반영 성공 후 화면 갱신 · 전달 방식은 미확정')
    d.arrow(2,1,'주문 상태·체결·자산 결과',True)
    d.arrow(1,0,'처리 결과 표시',True)
    d.band('응답 유실·INTERNAL_ERROR·DB 실패: 결과 확인·복구·중복 정산 방지 정책 별도 설계',True)

ACTORS=[('서비스 백엔드','요청자'),('Router / Protocol','I/O 스레드'),('Engine Worker','엔진 작업 큐'),('Engine','단일 종목 상태')]
JOBS=[
 ('01-new-order','01  신규 주문 처리','구현된 흐름 · 지정가 매수/매도 → 검증 → 가격·시간 우선 매칭',ACTORS,1600,1640,order,'실선: 요청·처리 전달   /   점선: 결과 반환   ·   결과는 요청한 연결로만 전달하며 체결 방송은 없음'),
 ('02-cancel-order','02  미체결 잔량 취소','구현된 흐름 · 취소 성공을 확인한 뒤 잔량과 상태를 반영',ACTORS,1600,1570,cancel,'이미 체결된 거래는 유지 · 현재가는 바뀌지 않음 · 먼저 전량 체결되면 ALREADY_FILLED'),
 ('03-current-price','03  현재가 조회','구현된 흐름 · 이 엔진의 마지막 실제 체결 가격을 한 번 조회',ACTORS,1600,1320,price,'최초 체결 전에는 null · 주문 대기·거절·취소는 현재가를 변경하지 않음 · 자동 푸시는 없음'),
 ('04-service-order','04  전체 서비스 주문 흐름','통합 설계 제안 · 사용자 입력 → 자산 예약 → 매칭 → DB 정산 → 화면 갱신', [('사용자','주문 주체'),('프론트엔드','외부 영역'),('서비스 백엔드','외부 영역'),('서비스 DB','외부 영역'),('매칭 서비스','현재 저장소')],1900,1820,service,'매칭 서비스 내부는 01번 참조 · 외부 단계는 제안이며, 전체 흐름을 보장하는 원자적 트랜잭션은 없음'),
]
for stem,title,subtitle,actors,w,h,draw,footer in JOBS:
    for fmt in ['png','svg']:
        file=OUT/f'{stem}.{fmt}'
        surface=surface_image(0,w*2,h*2) if fmt=='png' else surface_svg(str(file).encode(),w,h)
        ctx=create(surface)
        if fmt=='png': scale(ctx,2,2)
        diagram=Diagram(ctx,w,h,title,subtitle,actors,footer)
        draw(diagram)
        assert diagram.y < h-100, (stem,diagram.y,h)
        if fmt=='png': assert png(surface,str(file).encode())==0
        destroy(ctx); finish(surface); surface_destroy(surface)
        print(file.name)
with zipfile.ZipFile(OUT/'sequence-diagrams.zip','w',zipfile.ZIP_DEFLATED) as z:
    for stem,*_ in JOBS:
        for fmt in ['png','svg']:
            path=OUT/f'{stem}.{fmt}'
            z.write(path,path.name)
