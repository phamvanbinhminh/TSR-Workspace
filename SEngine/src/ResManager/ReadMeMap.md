!cấu trúc file .map(plain text)
[MAIN]
type=S/C <- Server/Client
w=<chiều w>
h=<chiều h>
UnitSize=<kích thước mỗi vùng AxA>
!vẽ từng x/region_S_y(C) đến w,region_S_h(C)  


!cấu trúc region
/*
cả Server và Client cũng theo từng vùng UnitSizexUnitSize(mỗi region là UnitSizexUnitSize)
!cấu trúc region_S_y.dat
/*
char[]; REGION_DATA_S
[Obstacle Table]
!ví dụ Obstacle Table
danh sách đỉnh của vật cản <- thông tin vật cản (với mỗi x,y là toạ độ của vật cản tính từ hệ quy chiếu của region không tính từ gốc toạ độ của cả map)
//
!cấu trúc region_C_y.dat
/*
char[]; REGION_DATA_C
int layerCount;
[Obstacle Table]
!ví dụ Obstacle Table
danh sách đỉnh của vật cản <- thông tin vật cản (với mỗi x,y là toạ độ của vật cản tính từ hệ quy chiếu của region không tính từ gốc toạ độ của cả map)

[String Table layer0]
[String Table layer1]
[String Table layer2]
...

!vẽ theo thứ tự các string table
!String Table là Danh sách tên file:
thư mục chứa spr(nếu có)/<tên file>.spr <- đã có cách draw ở ReadMeSpr.md
//
//

!cấu trúc folder map
/<tên map>
	<tên map>.png <- minimap toàn cục
	<tên map>.map <- settings map
	/Region
		các thư mục con dạng x có các file region_S_y.dat hoặc region_C_y.dat tuỳ vào bản nào