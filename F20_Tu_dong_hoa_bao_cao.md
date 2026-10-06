# Tự động hóa máy đo F20

Ngày: 2026-10-05

> **Cập nhật 06/10/2026:** đã quyết định viết toàn bộ bằng C++ (cầu nối
> C++/CLI, không dùng C#) và giao tiếp mạng chuẩn hóa theo MQTT — xem
> `F20_Qt_Software_Specification.md`.

## Kết luận

**Có thể làm được, và khả thi.** Hãng (Filmetrics, nay thuộc KLA) có sẵn một thư
viện lập trình tên **FIRemote**. Dự kiến phần mềm này dùng thư viện này để ra lệnh
cho F20 đo, rồi lấy kết quả (độ dày màng, độ khớp GOF, phổ đo) gửi sang hệ thống
khác.

Điều kiện bắt buộc:

- Cần **một máy tính Windows** (dùng một máy NUC nhỏ) đặt cạnh F20, cài phần mềm
  FILMeasure của hãng. F20 chỉ nối với máy này qua cáp USB.
- Trên NUC chạy một **chương trình cầu nối** tự phát triển (C#/.NET). Chương trình
  này nói chuyện với FILMeasure và với máy chủ Linux qua mạng.
- Máy chủ Linux **không điều khiển F20 trực tiếp được**. Mọi lệnh đều đi qua
  chương trình cầu nối trên NUC.

Việc tính toán (khớp mô hình màng để ra độ dày) **vẫn do FILMeasure làm**. Không
cần tự viết thuật toán đo.

## Cách hoạt động

```
┌──────────────────── Máy chủ Linux ────────────────────┐
│ Màn hình giám sát, cơ sở dữ liệu, SPC, cảnh báo       │
│ Gửi lệnh: đo, chọn recipe, hiệu chuẩn                 │
└───────────┬───────────────────────────▲───────────────┘
       lệnh │                           │ kết quả, trạng thái, phổ
            ▼   mạng LAN: REST / WebSocket / MQTT
┌──────────────────── Máy NUC · Windows 10/11 ──────────┐
│  ┌──────────────────────────┐ FIRemote ┌────────────┐ │
│  │ Chương trình cầu nối (C#)│ ◄──────► │ FILMeasure │ │
│  │ (phần cần tự phát triển) │          │ tính độ dày│ │
│  └──────────────────────────┘          └─────┬──────┘ │
└──────────────────────────────────────────────┼────────┘
                                               │ USB 2.0
┌──────────────────────┐   sợi quang   ┌───────▼──────────┐
│ Sản phẩm trên máy    │ ◄───────────► │ Máy đo F20       │
│ Đầu đo inline LA1-RKM│               │ Đèn + quang phổ  │
│ cách ~150 mm         │               │ (không có mạng)  │
└──────────────────────┘               └──────────────────┘
```

Máy chủ Linux gửi lệnh qua mạng tới NUC. Chương trình cầu nối gọi FILMeasure qua
FIRemote, FILMeasure điều khiển F20 qua USB, rồi kết quả đi ngược lại theo cùng
đường.

## Điều khiển được gì

| Việc cần làm | Lệnh FIRemote | Ví dụ dùng trên dây chuyền |
|---|---|---|
| **Đo một sản phẩm** (đo + tính) | `Measure` | Máy chủ báo "đo sản phẩm số 123", nhận lại kết quả |
| Chọn công thức đo (recipe) theo sản phẩm | `SetRecipe` | Quét mã vạch → tự chọn đúng recipe |
| Hiệu chuẩn (baseline) | `BaselineSetRefMat`, `BaselineAcquireSpectrumFromSample`, `BaselineAcquireReference`, `BaselineAcquireBackgroundAfterRef`, `BaselineCommit` | Hiệu chuẩn theo lịch, mỗi ca hoặc mỗi 30 phút |
| Khôi phục hiệu chuẩn cũ | `AuthenticateRefBac` | Khởi động lại nhanh sau khi mất điện |
| Mở cửa sổ hướng dẫn hiệu chuẩn cho công nhân | `BaselineShowDialog` (*) | Công nhân làm theo từng bước trên màn hình |
| Chỉ lấy phổ, tính sau | `AcquireSpectrum`, `AnalyzeSpectrum` | Đo nhanh, tính lại bằng recipe khác |
| Tính lại dữ liệu cũ | `OpenSpectrum`, `AnalyzeSpectrum` | Kiểm tra lại lô hàng cũ mà không cần đo lại |
| Chỉnh mô hình màng | `SetThickness`, `SetRoughness`, `SetMaterial` (*), `SetAnalysisWavelengthRange` (*) | Tinh chỉnh theo từng loại sản phẩm |
| Lưu file phổ | `SaveSpectrum` (.csv, .txt, .spe, .fmspe) | Lưu dữ liệu gốc để truy xuất |
| Xoá lịch sử đo | `HistoryDeleteAllResults` (*) | Bắt đầu lô mới |
| Ẩn / hiện giao diện FILMeasure | `New(...)`, `GUIVisible` (*) | Ẩn khi sản xuất, hiện khi bảo trì |
| Xuất tín hiệu số ra ngoài | `GeneralPurposeIOSetValue` (*) | Báo đạt/không đạt cho PLC, chỉ khi máy có phần cứng I/O |

(*) Chỉ có từ FILMeasure bản 7 trở lên (sách hướng dẫn 2013). Cần kiểm tra bản
đang cài ở mục Help → About FILMeasure.

## Đọc được dữ liệu gì

Mỗi lần đo trả về độ dày từng lớp màng và độ tin cậy của phép đo. Máy chủ Linux
dùng các dữ liệu này để hiển thị, lưu trữ và cảnh báo.

| Dữ liệu | Lấy từ | Cách tận dụng |
|---|---|---|
| **Độ dày từng lớp, GOF** (độ khớp 0–1, 1 là khớp hoàn toàn) | Kết quả của `Measure` / `AnalyzeSpectrum` | Xét đạt/không đạt, biểu đồ SPC, theo dõi xu hướng |
| Chiết suất n, k | Cùng kết quả (nếu recipe có tính) | Kiểm tra vật liệu, quy trình phủ |
| Tóm tắt kết quả dạng chữ | `ResultsSummary` | Hiển thị ngay trên màn hình giám sát |
| Phổ gốc (bước sóng + độ phản xạ) | `AcquireSpectrum`, `SaveSpectrum` | Lưu trữ, phân tích thêm, đo lại khi cần |
| Hiệu chuẩn còn hợp lệ không | `BaselineExistsAndIsAuthenticated` (*) | Chặn đo khi chưa hiệu chuẩn |
| Tình trạng tín hiệu, đèn | `SpectrometerDiagnostics` (*) | Cảnh báo sớm đèn yếu, sợi quang bẩn |
| Thông tin recipe đang dùng | `RecipeInfo` (*) | Hiển thị cấu trúc màng đang đo |
| Tên và số serial máy | `MeasChannelHWName`, `MeasChannelHWSerialNumber` | Gắn số máy vào từng kết quả để truy xuất |
| Tín hiệu số đầu vào | `GeneralPurposeIOReadValue` (*) | Cảm biến "có sản phẩm", chỉ khi máy có phần cứng I/O |

(*) Từ FILMeasure bản 7 trở lên.

Từ các dữ liệu này, máy chủ Linux có thể làm:

- Màn hình giám sát trực tiếp: độ dày, GOF, đạt/không đạt theo từng sản phẩm.
- Biểu đồ SPC: phát hiện quy trình trôi trước khi ra hàng lỗi.
- Quản lý hiệu chuẩn: theo dõi tuổi baseline, nhắc hoặc tự chạy hiệu chuẩn.
- Cảnh báo bảo trì: tín hiệu giảm dần nghĩa là sắp phải thay đèn.
- Lưu trữ phổ gốc: có thể tính lại hàng cũ khi đổi tiêu chuẩn.

## Giới hạn và rủi ro

Không có giới hạn nào chặn dự án. Tất cả đều có cách xử lý.

| Giới hạn | Ảnh hưởng | Cách xử lý |
|---|---|---|
| Linux không điều khiển F20 trực tiếp được | Bắt buộc có máy Windows | Dùng NUC chạy Windows 10/11 làm cầu nối |
| FILMeasure không có sẵn giao tiếp mạng (TCP, OPC-UA, SECS/GEM) | Phải tự viết phần kết nối mạng | Chương trình cầu nối mở cổng mạng (REST, WebSocket hoặc MQTT) |
| Hiệu chuẩn cần đặt tấm chuẩn (wafer Si) dưới đầu đo | Chưa tự động 100% được nếu không có cơ khí | Công nhân làm mỗi ca, hoặc làm thêm vị trí chuẩn cố định trên máy |
| Độ ổn định của hiệu chuẩn | Màng mỏng dưới 100 nm cần hiệu chuẩn lại mỗi 20–30 phút | Lập lịch hiệu chuẩn tự động; bật đèn trước 10 phút |
| Không tạo recipe mới bằng code được | Kỹ sư phải tạo recipe trên FILMeasure | Tạo sẵn recipe cho từng sản phẩm, code chỉ chọn và chỉnh |
| Tên các trường kết quả không ghi trong sách | Chưa biết chính xác tên biến | Tra trong Visual Studio (Object Browser) khi cài lên NUC |
| F20 không có bàn dịch chuyển | Không tự cấp/lấy sản phẩm | Máy sản xuất đưa sản phẩm vào; gắn đầu đo dạng inline (bộ LA1-RKM, cách sản phẩm khoảng 150 mm) |

## Nguồn tài liệu

- Sách hướng dẫn F20 bản 6.1.0 (2011), chương "Automation and Data", trang 83–100
  (`F20_User_Manual.pdf` trong thư mục này).
- [Sách hướng dẫn F20 bản 7.3.2.0 (2013)](https://www.egr.msu.edu/psp/sites/default/files/content/F20%20User%20Manual.pdf),
  chương "Software Automation".
- Datasheet F20 của KLA (2025): kết nối USB 2.0, Windows 10 trở lên
  (`F20 1.pdf` trong thư mục này).
- [Trang sản phẩm F20 của KLA](https://www.kla.com/products/instruments/thin-film-reflectometers/filmetrics-f20).
