#pragma once
// 콘솔 인코딩. 도구·서버의 메시지는 UTF-8(한국어)이다.
// Windows 콘솔(PowerShell·cmd)의 기본 코드 페이지는 cp949 라서 그대로 쓰면 글자가 깨진다 → 출력 코드 페이지를
// UTF-8(65001)로 바꾼다. 다른 OS 에서는 아무것도 하지 않는다. 프로세스 시작 때 한 번 부른다.
// 명령줄 인자도 같다: Windows 의 main(argc, argv) 는 시스템 코드 페이지(cp949)로 바뀐 바이트라 한글 이름 · 경로가
// UTF-8 이 아니다 (UTF-8 로 못 바꾸는 글자는 '?') → utf8Arguments 가 UTF-16 명령줄에서 다시 만든다 (Phase 10B).

#include <string>
#include <vector>

namespace sbx::console {

void useUtf8Output() noexcept;

// 명령줄 인자를 UTF-8 로 (0 번 = 실행 파일). Windows: GetCommandLineW → CommandLineToArgvW → UTF-8. 그 밖: argv 그대로
[[nodiscard]] std::vector<std::string> utf8Arguments(int argc, char** argv);

} // namespace sbx::console
