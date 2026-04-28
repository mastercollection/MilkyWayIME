# MilkyWayIME 한국어 문서

MilkyWayIME는 Windows TSF 기반 한글 입력기입니다. 입력 처리는 두 가지 자판 개념을 분리해서 다룹니다.

- 영어/base 자판: 사용자의 현재 키 라벨 배열입니다. 예: QWERTY, Colemak, Graphite.
- 한글 자판: libhangul이 조합에 사용하는 한글 배열입니다. 예: 두벌식, 세벌식 최종, 신세벌식.

입력 흐름은 다음과 같습니다.

```text
Windows/TSF 입력 라벨 -> 선택된 영어/base 자판의 역매핑 -> QWERTY/libhangul 토큰 -> libhangul 한글 조합
```

단축키는 한글 조합용 토큰이 아니라 Windows/TSF가 보고한 입력 라벨 기준으로 해석합니다.

## 영어/base 자판 추가

영어/base 자판은 JSON 파일로 추가합니다. 현재 런타임에서 지원되는 사용자 자판 형식은 이 JSON 형식입니다.

사용자 자판 파일 위치:

```text
%APPDATA%\MilkyWayIME\layouts\base
```

예를 들어 `my_colemak_variant.json` 파일을 위 폴더에 만들 수 있습니다.

```json
{
  "id": "my_colemak_variant",
  "displayName": "내 콜맥 변형",
  "keys": {
    "s": "r",
    "e": "f",
    "r": "p"
  }
}
```

`keys`의 방향은 반드시 다음과 같습니다.

```text
고정 QWERTY/libhangul 토큰 위치 -> 현재 영어/base 자판의 입력 라벨
```

예를 들어 `"s": "r"`은 “libhangul에 `s`로 보내야 하는 고정 위치가 현재 자판에서는 `r` 라벨에 있다”는 뜻입니다. 런타임에서는 이 매핑을 뒤집어서 처리합니다.

```text
입력 라벨 r -> QWERTY/libhangul 토큰 s -> libhangul 입력 "s"
```

생략한 키는 자기 자신으로 매핑됩니다. 따라서 QWERTY와 같은 키는 JSON에 적지 않는 것이 좋습니다.

지원되는 키 이름:

- `a`부터 `z`
- `0`부터 `9`
- 비Shift OEM 라벨: `;`, `/`, `` ` ``, `[`, `\`, `]`, `'`, `=`, `,`, `-`, `.`
- 표준 이름: `Space`, `Tab`, `Return`, `Backspace`, `Oem1`

Shift가 필요한 라벨인 `:`, `?`, `!` 같은 값은 base 자판 JSON에서 받지 않습니다. 같은 입력 라벨이 두 번 나오면 어느 토큰으로 되돌릴지 알 수 없으므로 잘못된 자판으로 처리됩니다.

JSON 파일을 바꾼 뒤에는 대상 앱을 다시 열어야 합니다. TSF 서비스가 만들어질 때 한 번만 읽고, 실행 중 자동 reload는 아직 없습니다. 잘못된 JSON 파일은 건너뛰고 다른 정상 파일은 계속 사용합니다. 기존 `us_qwerty`, `colemak`과 같은 ID를 다시 정의하면 사용자 정의가 우선합니다.

## 한글 자판 규칙

한글 조합은 MilkyWayIME가 직접 JSON으로 해석하지 않고 libhangul에 위임합니다. MilkyWayIME의 한글 자판 ID는 다음 형식을 씁니다.

```text
libhangul:<libhangul-keyboard-id>
```

예:

```text
libhangul:2
libhangul:3f
libhangul:3sin-1995
libhangul:3sin-p2
```

현재 내장 목록에는 두벌식, 세벌식 계열, 로마자, 안마태, 신세벌식 1995, 신세벌식 P2가 포함됩니다. 신세벌식처럼 숫자나 기호 키까지 한글 조합에 쓰는 자판은 비문자 ASCII도 libhangul로 전달되도록 등록해야 합니다.

세벌식 계열의 갈마들이는 libhangul의 `HANGUL_IC_OPTION_AUTO_REORDER` 옵션을 켜서 처리합니다. MilkyWayIME 자체 설정으로 켜고 끄는 UI는 아직 없습니다.

## 한자 변환

한자 후보는 libhangul 번들 데이터에서 생성한 C++ 정적 테이블로 조회합니다.
런타임은 `hanja.bin`, `mssymbol.bin`, txt 파일을 로드하지 않습니다. 한자/심볼
데이터를 갱신할 때는 `tools\generate-static-hanja.cmd`를 실행하고 생성된
`src\adapters\dictionary\generated_hanja_data.cpp`를 함께 커밋합니다.

- 조합 중인 한글 음절에서는 기존처럼 한글→한자 후보를 표시합니다.
- 선택 영역이 있으면 선택 시작 prefix만 봅니다. 한글 prefix는 한자 후보, 한자 prefix는 한글 역변환 후보를 표시합니다.
- 선택 영역이 없고 조합 중도 아니면 caret 바로 앞의 연속 한글 또는 한자 run만 봅니다. 공백이나 문장부호를 넘어 이전 단어를 찾지 않습니다.
- caret 변환 후보창이 열린 상태에서 Hanja 키를 다시 누르면 현재 후보를 확정하지 않고 다음 사전 segment 후보로 이동합니다.
- 형태소 분석, 조사 분리, 선택 영역 내부 검색은 아직 하지 않습니다.

## 커스텀 한글 자판

커스텀 한글 자판은 MilkyWayIME JSON이 아니라 libhangul XML 형식을 기준으로 설계합니다.

예:

```xml
<hangul-keyboard id="my-sebeol" type="jamo">
  <name xml:lang="ko">내 세벌식</name>
  <map id="0">
    <item key="0x71" value="0x1107"/>
  </map>
</hangul-keyboard>
```

이 XML의 `id="my-sebeol"`은 MilkyWayIME에서 다음 ID로 참조합니다.

```text
libhangul:my-sebeol
```

현재 한계:

- 현재 빌드는 `ExternalKeyboard=NO`, `ENABLE_EXTERNAL_KEYBOARDS=0`으로 libhangul을 빌드합니다.
- 따라서 사용자 XML 한글 자판을 런타임에서 바로 읽는 기능은 아직 켜져 있지 않습니다.
- 커스텀 한글 자판을 실제 런타임 기능으로 만들려면 libhangul 외부 자판 로딩과 expat 의존성을 빌드에 포함해야 합니다.
- 신세벌식처럼 한 키가 초성/중성/종성 후보를 함께 가질 수 있는 stateful 자판은 일반 libhangul XML만으로 충분하지 않을 수 있습니다. 그런 경우 libhangul 쪽 자판 타입 확장이나 별도 key value 해석 규칙이 필요합니다.

## 설치 프로그램

첫 설치 프로그램 대상은 x64 WiX v6 MSI입니다.

```powershell
.\installer\build-installer.ps1
```

이미 바이너리가 빌드되어 있으면 빠른 MSI 재패키징만 실행할 수 있습니다.

```powershell
.\installer\build-installer.ps1 -SkipSolutionBuild
```

WiX/MSI ICE validation은 몇 분씩 걸릴 수 있어서 기본값에서는 생략합니다.
전체 validation이 필요할 때만 `-ValidateMsi`를 붙입니다.

GitHub Actions workflow는 `.github\workflows\build-installer.yml`에 있습니다.
`windows-2025-vs2026` 이미지에서 실행되고, 설치된 MSVC toolset 목록을
로그에 출력한 뒤 같은 Release x64 MSI를 빌드해서 artifact로 업로드합니다.
MSI 버전은 `vX.Y.Z` 태그, 수동 workflow 입력값, 또는
`0.1.<GITHUB_RUN_NUMBER>`에서 자동으로 정합니다.

스크립트는 `Release|x64`를 빌드하고 `mwime_tsf.dll`, `us_qwerty` /
`colemak` base layout JSON 샘플을 MSI에 포함합니다. 한자/심볼 데이터는 DLL에
정적 테이블로 컴파일됩니다.

출력 파일:

```text
build\installer\bin\Release\MilkyWayIME.msi
```

MSI는 `%ProgramFiles%\MilkyWayIME`에 설치하고 elevated custom action에서
`regsvr32`를 실행합니다. 따라서 `DllRegisterServer` /
`DllUnregisterServer`가 COM 등록과 TSF profile/category 등록을 처리합니다.

현재 한계:

- x64 전용입니다.
- TSF profile을 등록하지만 현재 사용자에게 `InstallLayoutOrTip`으로 즉시 추가/활성화하지는 않습니다.
- DIME식 locked DLL rename/copy-back 처리는 아직 없습니다. 업그레이드나 제거 전에는 IME를 사용하는 앱을 닫는 것이 안전합니다.

Release 바이너리는 정적 MSVC 런타임(`/MT`)으로 빌드하므로 MSI가 Visual C++
Redistributable을 포함하거나 다운로드할 필요는 없습니다.

## 문서 위치

- 영어/base 자판 샘플: `data/layouts/base`
- 기존 영어 설명: `data/layouts/README.md`
- 구조 원칙: `docs/architecture/initial-structure.md`
