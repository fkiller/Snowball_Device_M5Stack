<p align="center">
  <img src="assets/banner.png" alt="Snowball Banner" width="100%">
</p>

<h1 align="center">
  <img src="assets/icon.png" width="48" height="48" valign="middle" alt="Snowball Icon">
  Snowball Device · M5Stack — Preview
</h1>

<p align="center"><strong>오리지널 ESP32 Core + FACES 키보드 펌웨어 및 기기 게이트웨이</strong></p>

<p align="center">
  <a href="README.md">English</a> | <a href="README.ko.md">한국어</a>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-Apache_2.0-blue.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/Node.js-%3E%3D22.12-green.svg" alt="Node.js">
  <img src="https://img.shields.io/badge/Platforms-ESP32%20%7C%20FACES-orange.svg" alt="Platforms">
  <a href="https://github.com/fkiller/Snowball_Middleware#one-shot-install"><img src="https://img.shields.io/badge/Install-Snowball-purple.svg" alt="Install Snowball"></a>
</p>

---

<a id="one-shot-install"></a>
## Snowball 한 번에 설치

**PC마다 Snowball을 한 번 설치하세요.** 기본 설치에 Web UI, MK20 검색·연결·음성 런타임, M5Stack 검색·게이트웨이, Codex·Antigravity·OpenCode 하네스 플러그인 3종이 모두 포함됩니다. 기기 프로필을 고르거나 USB 기기를 연결할 필요가 없습니다.

Windows PowerShell에서 실행하세요.

```powershell
& ([scriptblock]::Create((irm 'https://raw.githubusercontent.com/fkiller/Snowball_Middleware/main/install.ps1')))
```

설치기는 Node/Git/Python과 구성 요소를 준비하고 실제 격리 하네스 워커를 확인한 뒤, 트레이 아이콘이 있는 사용자별 숨김 백그라운드 앱을 시작합니다. 소유 런타임이 응답하면 설치 터미널로 돌아옵니다. 트레이에서 Web UI, 상태, 설정, 일시정지/재개, 재시작, 종료, 로그인 자동 시작을 제공합니다. Web UI는 **http://127.0.0.1:8765/**이며 기본 설치 위치는 `%LOCALAPPDATA%\Snowball`입니다. **Start-Snowball.ps1**은 다운로드 없이 다시 실행합니다. 네이티브 하네스 앱과 로그인은 공급자를 통해 준비하며, 없으면 사용 불가로 표시합니다.

기존 설치 업데이트는 같은 `-InstallRoot`에 `-Update`를 추가하고 `-Profile`은 생략하세요. 기존 단일 기기 설치에도 두 어댑터를 준비하며 기기 설정·페어링 키·컨트롤러 상태·음성 캐시·사용자 지정 API 포트·로그인 자동 시작 설정을 보존합니다. 사설 LAN이 없거나 선택이 모호하면 자동 검색은 대기·재시도하고 Web UI는 유지합니다. 필요하면 `-Bind PC의_사설_IP`를 지정하세요.

MK20은 Wi-Fi에 독립적으로 연결한 뒤 K17(Machines)에서 PC를 선택해 페어링합니다. M5Stack 펌웨어 **0.4.0 이상**은 같은 LAN의 등록 PC와 새 미들웨어를 자동 검색합니다. 머신 목록에서 새 PC를 선택하면 Wi-Fi로 페어링합니다. PC 추가에는 전체 미들웨어 설치만 필요하며 기기별 추가 설치·USB 연결·등록 스크립트는 필요하지 않습니다. 같은 브로드캐스트 서브넷, 기기 포트 허용, 클라이언트 격리 해제 상태의 신뢰하는 사설 LAN을 사용하세요.

M5Stack 최초 준비나 펌웨어 갱신은 **USB 연결 시 자동으로 열리는 기기 설정 창**에서 진행합니다. 트레이의 **기기 설정…**에서도 열 수 있습니다. 현재/제공 버전과 개선 사항을 보여주며, 사용자가 설치·업데이트를 선택해야 전체 플래시 비공개 백업 → 펌웨어 쓰기 → Wi-Fi·페어링 저장 영역 보존 검증 → 자동 재기동 확인을 수행합니다. 현재 제공 펌웨어는 **0.5.0**입니다. 저장된 네트워크가 없으면 기본 선택된 가져오기 옵션으로 업데이트 후 PC의 연결된 Wi-Fi를 OS가 허용하는 범위에서 이관하고, 기존 정보가 있으면 덮어쓰지 않습니다. OS 비밀번호는 해당 선택 이후에만 읽으며 UI·로그에 표시하지 않습니다. 가져올 수 없으면 같은 창에서 설정 → Wi-Fi, 네트워크 선택, FACES 키보드 비밀번호 입력을 그림으로 안내합니다. 최초 ESP32 Core 4MB/16MB만 지원합니다. CP210x만으로 기종을 확정하지 않으며 Snowball 응답이 없는 기기는 사용자가 실제 기종을 확인해야 합니다. 빌드는 누락된 도구를 다운로드할 수 있습니다. 0.3.0 이하는 새 PC LAN 검색을 위해 기기 펌웨어를 한 번 업그레이드해야 합니다. `Register-M5Stack.ps1` / `.sh`는 선택적 레거시 유지보수 도구로 남으며 일반 설치와 `-Update`가 기기를 자동 플래싱하지 않습니다.

MK20과 미래 기기를 포함한 모든 디바이스는 같은 **기기 설정…** 여정(감지·버전/개선 사항·동의·검증한 복구 백업·설정 보존·업데이트·재기동·네트워크 설정/시각 안내)을 따릅니다. 지원 어댑터는 기본 설치에 포함하며 별도 등록 스크립트가 필요 없습니다. MK20은 같은 창에서 QMK DFU와 T113/SD 런타임을 각각 다룹니다. QMK는 변경할 앱·설정 영역 전체를, SD는 카드 전체 이미지를 쓰기 전에 백업합니다. SD를 기기에 돌려 넣는 동작은 시각적으로 안내하며 실제 LAN 부팅·버전·실행 파일 해시 응답을 받아야 완료됩니다. 일반 MK20 LAN 페어링은 USB/ADB 없이 동작합니다. 실물 플래싱·SD 재삽입·macOS 인수 검증은 별도입니다. 네이티브 창은 검색된 기기 종류를 함께 표시하며 MK20의 실제 LAN 검색도 받습니다. 구 펌웨어가 버전을 응답하지 않으면 추정하지 않고 미확인으로 표시합니다.

옵션: `-Update`, `-InstallRoot 경로`, `-Bind PC의_사설_IP`, `-Port 8765`, `-NoStart`, `-NoShortcut`. `-Profile web|mk20|m5stack`은 개발·진단용 명시적 부분 구성으로 유지하며 일반 설치에서는 필요 없습니다. 같은 루트에 이미 설치된 어댑터는 유지합니다. Node/Git/Python이 준비된 macOS 소스 환경에서는 같은 부모 폴더 체크아웃으로 `npm run setup --`을 실행하며, USB 준비는 `--prepare-m5stack [--no-flash]`로 명시합니다. Linux 전체 워커 런타임은 아직 지원하지 않습니다. 프로토콜·인증·실물 검증 범위는 중앙 명세를 확인하세요.

[공통 아키텍처와 저장소 역할](https://github.com/fkiller/Snowball_Control/blob/main/docs/ARCHITECTURE.md)

[Control · MK20](https://github.com/fkiller/Snowball_Control) · [Middleware · Installer / Web UI](https://github.com/fkiller/Snowball_Middleware) · [Device · M5Stack](https://github.com/fkiller/Snowball_Device_M5Stack) · Harness: [Codex](https://github.com/fkiller/Snowball_Harness_Codex), [Antigravity](https://github.com/fkiller/Snowball_Harness_Antigravity), [OpenCode](https://github.com/fkiller/Snowball_Harness_OpenCode)

---

오리지널 M5Stack Core / Gray와 FACES QWERTY용 네이티브 ESP32 펌웨어 및 Snowball Protocol 1 디바이스 플러그인입니다. 첫 버전은 영문과 한글 두벌식 키보드 입력을 지원하며, 음성을 녹음하거나 PC 마이크를 사용하지 않습니다.

## 실기 구동 영상

실제 M5Stack + FACES에서 세션 내용을 탐색하고, 표시 언어를 바꾸고, 키보드로 프롬프트를 작성하는 모습입니다. 사용자가 2026-10-04에 공개한 실기 영상 원본입니다.

<p align="center">
  <a href="assets/videos/m5stack_navigation_demo.mp4"><img src="assets/screenshots/m5stack_navigation_demo.gif" width="360" alt="실제 M5Stack + FACES 버튼 탐색 시연"></a><br>
  <em>24초 발췌 미리보기 · 전체 영상 1분 34초</em><br>
  <a href="assets/videos/m5stack_navigation_demo.mp4">▶ GitHub 전체 MP4</a> &nbsp;|&nbsp; <a href="https://x.com/fkiller/status/2106892916149158101?s=20">X 원본 글</a>
</p>

## 기기와 실제 구동 화면

<a href="https://docs.m5stack.com/en/core/Faces_Kit"><img src="https://static-cdn.m5stack.com/resource/docs/products/core/face_kit/face_kit_01.webp" alt="M5Stack Core와 FACES QWERTY 및 다른 입력 패널" width="420"></a>

제품 사진은 © M5Stack의 [공식 FACES Kit 문서](https://docs.m5stack.com/en/core/Faces_Kit)에서 제공하는 하드웨어 참고 사진입니다. 아래 이미지는 실제 연결된 기기의 펌웨어 화면입니다.

| 설정 | 표시 언어 | 입력 설정 |
| --- | --- | --- |
| ![실제 한글 설정 화면](assets/screenshots/ko/settings.png) | ![실제 표시 언어 선택](assets/screenshots/ko/display-language.png) | ![실제 입력 설정](assets/screenshots/ko/input-settings.png) |

![메뉴에 포커스가 돌아와도 Content에 메인 설정을 유지하는 실제 화면](assets/screenshots/ko/menu-focus.png)

| 영문 입력 | 한글 입력 | 접속 상태 |
| --- | --- | --- |
| ![한글 화면에서 영문 입력](assets/screenshots/ko/english-input.png) | ![실제 한글 입력](assets/screenshots/ko/korean-input.png) | ![실제 인증된 연결 성공 화면](assets/screenshots/ko/connected.png) |

| Fn 컨트롤 | 모델 팝업 | Effort 팝업 | Access 팝업 |
| --- | --- | --- | --- |
| ![실제 Fn 컨트롤](assets/screenshots/ko/fn-controls.png) | ![실제 모델 목록](assets/screenshots/ko/fn-model.png) | ![실제 모델별 Effort](assets/screenshots/ko/fn-effort.png) | ![실제 승인 정책 목록](assets/screenshots/ko/fn-access.png) |

![저장 AP의 암호가 자동 입력되고 가려진 실제 화면](assets/screenshots/ko/saved-password.png)

2026-10-04에 실제 M5Stack 펌웨어 0.2.2의 320×240 프레임버퍼에서 캡처했습니다. **표시 언어와 입력 언어를 독립적으로 선택하고 저장합니다.** 한글 화면에서 영문을 입력하거나 영문 화면에서 한글을 입력할 수 있습니다. 실제 세션 이름·경로·대화 내용은 원래 언어를 유지합니다. 암호 보이기를 켜도 진단 캡처에는 Wi-Fi 암호가 가려집니다.

![실제 하네스 원본 컬러 아이콘](assets/screenshots/ko/harness-list.png)

Breadcrumb는 메뉴, 하네스 원본 아이콘, 괄호 없는 프로젝트 이름, 세션 이름을 표시합니다. 하네스에 포커스를 주면 전체 이름이 나타납니다. 하단 Home / End는 **Hm / Ed**로 표시합니다. 나머지 동작에는 [Lucide](https://lucide.dev/) 아이콘을 사용하고, 누르기 표시는 같은 크기·기준선으로 맞춘 점 / 두 점 / 선입니다. 각각 한 번 / 두 번 / 길게 누르기입니다. 두 점은 원본 ellipsis의 가운데 원만 뺀 변형입니다. Lanczos 축소와 Floyd-Steinberg 디더링으로 RGB332 픽셀을 생성합니다. `assets/footer`에 원본·고정된 출처 URL·해시·라이선스가 있고, `firmware/footer_icons.h`에 생성된 픽셀이 있습니다. 기기에서 이미지를 다운로드하거나 변환하지 않습니다.

하네스 아이콘은 설치된 공식 OpenAI 확장의 Codex 앱 아이콘, [Google Antigravity 공식 배포 자료](https://antigravity.google/press), [OpenCode favicon](https://github.com/anomalyco/opencode/blob/dev/packages/ui/src/assets/favicon/favicon-96x96-v3.png)을 16×16 RGB332로 축소·디더링하고 RGB565로 전송합니다. 각 플러그인이 자신의 픽셀과 투명도 마스크를 정의합니다.

갤러리의 입력은 일회성 USB IME 진단으로 넣었으며 하네스에 프롬프트를 보내지 않았습니다. 실제 세션 대화는 갤러리에 넣지 않습니다. 이 캡처는 실제 렌더러를 확인하며, 위 실기 영상은 물리 버튼 탐색과 키보드 프롬프트 작성을 보여줍니다. 네이티브 하네스 응답 완료는 아직 검증하지 않았습니다.

아키텍처와 배포 검증은 [Snowball_Control/docs/ARCHITECTURE.md](https://github.com/fkiller/Snowball_Control/blob/main/docs/ARCHITECTURE.md) 한 곳에서 관리합니다. 0.3.0 이후는 미들웨어의 `/v1/controller` API와 플러그인 표시 메타데이터를 사용합니다. 기기마다 세션·모델·Effort·Access·테마·스크롤을 복구하고, 표시와 입력 언어는 각각 기기에 저장합니다. 유효한 세션 선택이 없으면 실제 미들웨어에서 마지막으로 활동한 세션과 하네스·프로젝트를 엽니다. 이후 다른 기기의 활동이 명시적으로 선택한 세션을 바꾸지 않습니다. M5Stack 조작은 MK20나 웹 탭의 선택을 바꾸지 않습니다.

## 빌드와 업로드

일반 사용자는 트레이의 기기 설정 창을 사용합니다. 개발자 유지보수에는 Node ≥22.12, Python 3.12, CP210x 드라이버가 필요합니다. 아래 도구는 항상 전체 플래시를 백업하고 NVS 보존을 확인한 뒤 재기동합니다. UART VID/PID만으로 제품을 식별하지 말고 실제 보드의 포트를 지정하세요.

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt
.venv\Scripts\python -m platformio run
.venv\Scripts\python scripts/install_device.py --port COM7 --output .local/device-install.json
```

기본 빌드 설정은 이전에 실물 확인한 16MB ESP32-D0WDQ6-V3용입니다. 설정 창·도구는 실제 4MB/16MB 용량을 읽어 로컬 빌드 설정을 자동 생성합니다. 3MB 앱 파티션은 OTA를 지원하지 않으며 업데이트는 USB로 합니다. 직접 PlatformIO 업로드는 제품의 백업·보존 검사를 우회하므로 지원되는 업데이트 경로가 아닙니다.

## 실행

펌웨어 0.4.0은 PC 등록 최대 16개를 보존합니다. 머신 목록은 등록 PC와 LAN에서 새로 발견한 미들웨어를 표시합니다. 새 PC 선택으로 USB 없이 등록하고, 등록 PC 선택으로 해당 PC의 하네스·프로젝트·세션 문맥을 복원합니다. 펌웨어 준비는 한 번의 기기 유지보수 단계입니다. TOFU 보안 경계와 검증 범위는 [중앙 문서](https://github.com/fkiller/Snowball_Control/blob/main/docs/ARCHITECTURE.ko.md)를 확인하세요.

[공통 Windows 설치기](https://github.com/fkiller/Snowball_Middleware/blob/main/README.ko.md#snowball-한-번에-설치)는 기본으로 두 기기를 포함하며 숨김 네이티브 트레이를 시작하고 터미널로 반환합니다. 트레이에서 Web UI, 설정, 일시정지/재개, 재시작, 종료와 로그인 자동 시작을 제공합니다. 일반 설치·업데이트에는 프로필 선택과 USB 준비가 필요 없습니다. 교차 구성 검증과 남은 실물 확인은 [중앙 아키텍처](https://github.com/fkiller/Snowball_Control/blob/main/docs/ARCHITECTURE.ko.md#변경-영향과-문서-동기화)에 기록합니다.

기존 Snowball 미들웨어를 루프백 8765에서 실행하세요. 게이트웨이는 실제 세션 저널·목록·모델 카탈로그·명령 API를 사용합니다. 첫 버전은 기존 세션을 선택하며 새 세션 생성은 미들웨어에서 관리합니다.

```powershell
node scripts/gateway.mjs --serial COM7 --python .venv\Scripts\python.exe
# 독립 Wi-Fi 제어: PC의 실제 사설 LAN 주소 지정
node scripts/gateway.mjs --serial COM7 --python .venv\Scripts\python.exe --bind YOUR_PRIVATE_LAN_IP
# PC의 인바운드 방화벽에 막히는 경우 인증된 아웃바운드 TCP 사용
node scripts/gateway.mjs --bind YOUR_PRIVATE_LAN_IP --device PAIRED_DEVICE_IP
```

기기에서 선택한 LAN 등록은 후보마다 새로 만든 키를 암호화해 전달하고 수신 확인 뒤 제어를 허용합니다. 키는 비공개 `.local/pairing.key`와 기기 NVS에 보관합니다. USB 등록은 선택적인 이전 방식·유지보수 경로입니다. 신뢰하는 사설 LAN에서 검색 UDP 47770과 등록·제어 TCP 47771을 허용해야 하며 Supervisor는 루프백에 남습니다. 등록 후에는 기기 47774로 인증된 아웃바운드 TCP도 사용할 수 있습니다. 기기 IP가 바뀌어도 새로 검증한 서명 HTTP 요청으로 주소를 갱신하며 USB가 필요하지 않습니다. 최초 등록은 신뢰 LAN TOFU이며 이후 제어는 인증된 평문입니다. 정확한 경계는 중앙 명세를 확인하세요.

## 조작

| 버튼 | 누르기 | 길게 / 반복 | 두 번 |
| --- | --- | --- | --- |
| A | ↑ / 왼쪽 | PgUp | Home |
| B | 선택 / 입력 | 하단에 표시되는 동작 | 하단에 표시되는 동작 |
| C | ↓ / 오른쪽 | PgDn | End |

세션 내용이 기본 화면입니다. Home으로 첫 줄에 간 뒤 ↑를 한 번 더 누르면 세션 Breadcrumb에 포커스가 갑니다. B로 현재 프로젝트의 세션 목록을 열며 현재 선택을 중앙에 배치합니다. 목록에서 Home 다음 ↑는 원래 Breadcrumb에 포커스를 주고 세션 내용으로 돌아갑니다. 설정 목록이나 하위 설정에서 맨 위를 넘으면 메뉴 아이콘에 포커스를 주면서 Content에는 메인 설정 목록을 유지합니다. 왼쪽 이동 순서는 **세션 → 프로젝트 → 하네스 → 머신 → 메뉴**입니다. 프로젝트와 하네스를 선택하면 실제 원천 목록이 열리고, 메뉴는 설정을 엽니다. 펌웨어 0.4.0의 머신 목록은 기존 등록 PC와 새로 발견한 PC를 표시합니다. PC를 선택하면 해당 PC의 실제 하네스·프로젝트·세션을 읽습니다.

세션 내용에서 B는 입력 편집, 길게 B는 모델·Effort·새로고침, 두 번 B는 최신 내용으로 이동합니다. 마지막 내용에서 아래로 이동하거나 읽는 중 키보드 문자를 누르면 입력 편집으로 들어갑니다. 첫 문자는 입력에 포함됩니다. 목록에서 길게 B는 뒤로, 두 번 B는 세션 내용으로 돌아갑니다.

| 입력 편집 버튼 | 누르기 | 길게 / 반복 | 두 번 |
| --- | --- | --- | --- |
| A | 커서 왼쪽 | 계속 왼쪽 | Home |
| B | 실행 | 영문 ↔ 한글 | 세션 내용으로 |
| C | 커서 오른쪽 | 계속 오른쪽 | End |

텍스트 맨 앞에서 A/길게 A/키보드 Left는 초안·커서·읽던 위치를 보존하고 세션 내용으로 돌아갑니다. 두 번 A는 Home이며 편집에 남습니다. Tab은 한/영 전환, Esc는 뒤로, Backspace는 커서 앞 삭제, Ctrl+U는 로컬 초안 삭제입니다. UTF-8 문자를 쪼개지 않고 중간 삽입·삭제하며, Shift 조합은 된소리·쌍모음을 지원합니다. 읽기와 입력 중 WASD는 실제 문자로 입력됩니다. B나 Enter는 실제 제어 가능한 네이티브 세션에만 전송합니다. 이 기기의 명령 ID가 저널에 접수된 경우에만 초안을 지우며, 거부·전달 불명확 시 보존하고 자동 재전송하지 않습니다.

표시 언어와 입력 설정을 선택하면 메인 설정 메뉴로 돌아오며 각각 원래 행에 커서를 복구합니다. Left(문자 입력이 아닌 메뉴에서는 A), Esc, 뒤로는 부모 메뉴의 원래 선택 항목으로 돌아갑니다. AP/암호 화면과 모델·Effort 메뉴에도 적용됩니다. Breadcrumb의 Left 이동과 텍스트 입력의 A 문자는 유지됩니다. 설정의 **표시 언어**는 English / 한국어, **입력 설정**은 영문 / 한글 두벌식입니다. Tab이나 입력 편집의 길게 B는 입력만 바꿉니다. SSID와 암호에는 키보드 문자를 그대로 입력합니다. UI 문자열은 `firmware/i18n.h`와 `src/i18n.mjs`에 모았으며 프로토콜 키·경로·모델 ID·네이티브 내용은 번역하지 않습니다.

입력 편집에는 **Model·Effort·Access**의 현재 값과 **Fn+Z**를 표시합니다. 공장 FACES 펌웨어는 Fn과 Alt 단독 입력을 내부에서 처리하고 Core에 전달하지 않습니다. [공식 키보드 소스](https://github.com/m5stack/FACES-Firmware/blob/master/KeyBoard.ino)에서 Fn+Z는 `0xBA`를 전달하므로 이를 사용합니다. Alt의 누름·뗌 상태도 이 I²C 프로토콜로 전송되지 않습니다. Alt를 누르고 있는 동안 하단을 바꾸려면 키보드 ATmega328의 ISP 배선과 별도 펌웨어 변경이 필요합니다. 현재 Core USB UART는 ESP32만 업로드합니다. Fn+Z로 하단을 Model·Effort·Access로 바꾸고 A·B·C로 해당 위치에 연결된 팝업을 엽니다. A/C 이동, B 선택, 길게 A/C는 페이지 이동, 두 번은 Home/End입니다. Fn+Z 또는 Esc로 선택하지 않고 취소하면 초안·커서·기존 설정을 보존합니다.

모델과 모델별 Effort는 실제 카탈로그에서 가져옵니다. Access는 설치된 Codex CLI가 생성한 `TurnStartParams` 스키마의 승인 정책을 `/v1/harness/access`로 발견해 다음 실행에 전달합니다. 파일 시스템 샌드박스를 바꾸지 않으며 네이티브 요구사항에 따라 거부될 수 있습니다. 지원하는 실제 전송 구현이 없는 하네스에는 Access 선택지를 제공하지 않습니다. 선택 값은 기기별 상태로 보관하고 명시적으로 프롬프트를 실행할 때만 전달합니다.

세션 내용은 하단 버튼 바로 위까지 **12줄**을 표시합니다. 스크롤바는 실제 표시 중인 내용의 오프셋과 전체 줄 수로 크기·위치를 계산합니다. 이전의 제어 가능 여부·모델·상태 영역과 고정된 포커스 선을 제거했습니다.

## Wi-Fi와 미들웨어 연결

2.4GHz AP 검색, 숨김 SSID, 암호 입력, 연결, 명시적인 저장 정보 삭제를 지원합니다. 이전에 성공한 SSID를 선택하면 저장된 암호를 미리 채웁니다. 최대 여덟 개의 성공한 네트워크를 저장하고, 실패한 시도는 기존 암호를 덮어쓰지 않습니다. 기존 단일 SSID 설정도 해당 AP의 다음 연결 성공 때 이전됩니다. 저장 Wi-Fi 삭제는 모든 프로필을 지웁니다.

접속 화면은 **Wi-Fi 접속 시도 → Wi-Fi 성공 → 미들웨어 접속 시도 → 미들웨어 성공**을 실제 결과에 따라 표시합니다. Wi-Fi 성공에는 실제 연결과 IP 할당이, 미들웨어 성공에는 해당 기기와 컨트롤러에 대한 인증된 응답이 필요합니다. 현재 SSID와 키가 일치하는 연결은 재시도에서도 유지해 무선 드라이버를 불필요하게 재시작하지 않습니다. 성공 화면을 1초 보여준 뒤 세션으로 이동합니다. Wi-Fi 실패는 입력을 유지하고 가린 상태로 연결 화면에 돌아가고, 미들웨어 실패는 검색 메뉴로 돌아갑니다.

AP 검색은 사용자가 요청할 때만 실행하며 검색·결과·오류를 표시합니다. 검색 중 다시 누르면 진행 중인 작업을 유지하고 완료 전까지 기존 AP 목록을 보존합니다. 암호 화면에서는 미들웨어 폴링과 화면을 바꾸는 USB 검색을 막습니다. 암호는 기본적으로 숨기며 Tab/길게 B로 표시·숨김을 바꿉니다. 진단 캡처는 항상 암호를 가립니다. A+B+C를 2.5초 누르면 기기 등록을 초기화하며 USB로 다시 등록할 수 있습니다.

## 플러그인과 검증

`src/manifest.mjs`는 실제 워커 해시를 계산합니다. 워커는 `devices.list`와 `devices.render`만 선언하고 루프백 브로커를 사용합니다. 시리얼·LAN과 실제 미들웨어 명령은 호스트 게이트웨이가 처리합니다. 기존 PluginHost는 프로세스 격리를 제공하며 커널 파일 시스템 샌드박스가 아니므로 검토한 플러그인만 로드하세요.

```powershell
npm test
powershell -File scripts/test-ime.ps1
# 같은 포트를 쓰는 USB 게이트웨이는 먼저 종료
python scripts/device_qa.py --port COM7 --output artifacts/home.png
python scripts/check-wifi.py --port COM7 --rounds 3 --require-aps
python scripts/check-navigation.py --port COM7 --screens artifacts/navigation-screens
python scripts/check-editor.py --port COM7
python scripts/check-ui.py --port COM7 --screens artifacts/ui-screens
python scripts/check-settings.py --port COM7 --screens artifacts/localized-screens
python scripts/check-settings.py --port COM7 --connect
python scripts/check-settings.py --port COM7 --saved-password
python scripts/check-settings.py --port COM7 --wifi-failure
```

네이티브 테스트는 펌웨어에서 사용하는 IME·네비게이션·연결 헤더를 직접 컴파일합니다. 연결 테스트는 실제 관측에 따른 전이와 시간 제한을 확인하며 타이머로 성공을 만들지 않습니다. USB 검사는 실제 기기의 핸들러·프레임버퍼를 사용하고 하네스 프롬프트를 보내지 않습니다. 기존 초안이나 편집이 있으면 일회성 검사를 거부합니다. 화면 캡처만으로 사람의 물리 키 입력이나 하네스 작업 완료를 확인했다고 주장하지 않습니다.

`node scripts/check-live.mjs ABSOLUTE_MIDDLEWARE_DIRECTORY PRIVATE_GATEWAY_IP`는 실제 워커·코어 레지스트리·서명된 검색/요청/응답·동적 세션/모델/Effort·재전송 차단을 검사합니다. 기기의 로컬 선택을 바꾸지만 하네스 프롬프트는 보내지 않습니다.

하단 아이콘을 다시 생성하는 방법:

```powershell
npm install --prefix artifacts/icon-builder --no-audit --no-fund @resvg/resvg-js@2.6.2
$icons = Get-ChildItem assets/footer/*.svg | ForEach-Object { $_.FullName }
node scripts/rasterize-icons.mjs $icons
python scripts/build-footer-icons.py
```

SVG 렌더러는 빌드용이며 펌웨어·게이트웨이의 실행 의존성이 아닙니다. 라이선스는 Apache-2.0이고 M5Unified·M5GFX·ArduinoJson·ESP32 및 아이콘의 원래 라이선스를 유지합니다. 아이콘 고지는 `assets/footer/LICENSE-Lucide`와 루트 `LICENSE`를 참고하세요.
