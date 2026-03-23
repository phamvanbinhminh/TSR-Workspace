Map File Structure
.map (plain text)
[MAIN]
type=S/C        ; S = Server, C = Client
w=<width>       ; số region theo trục X
h=<height>      ; số region theo trục Y
UnitSize=<A>    ; kích thước mỗi region (A x A)
Ghi chú
Map được chia thành lưới các region
Mỗi region có kích thước UnitSize x UnitSize
Tên region theo dạng:
region_<type>_<x>_<y>
Region System
Cả Server và Client đều dùng hệ thống region
Mỗi region là một ô trong grid
Toạ độ trong region là local, không phải global map
Server Region Data
region_S_<x>_<y>.dat
char[] REGION_DATA_S

[Obstacle Table]
Obstacle Table
Danh sách vật cản
Mỗi vật cản là một polygon (danh sách đỉnh)
Toạ độ là local trong region
Client Region Data
region_C_<x>_<y>.dat
char[] REGION_DATA_C
int layerCount

[Obstacle Table]

[String Table layer0]
[String Table layer1]
[String Table layer2]
...
Obstacle Table
Giống server
Dùng cho va chạm hoặc debug nếu cần
String Table

Mỗi layer là danh sách resource .spr:

<folder>/<file>.spr
Ví dụ
tiles/grass.spr
objects/tree.spr
Render
Vẽ theo thứ tự:
layer0 → layer1 → layer2 → ...
Cách render .spr xem ReadMeSpr.md
Folder Structure
/<map_name>
    <map_name>.png        ; minimap
    <map_name>.map        ; config

    /Region
        /0
            region_S_0_0.dat
            region_C_0_0.dat
            region_S_0_1.dat
        /1
            region_S_1_0.dat
            region_C_1_0.dat
Summary
Map chia thành grid region
Server dùng region_S (logic, collision)
Client dùng region_C (render, asset)
Asset client là .spr
Toạ độ trong region là local
