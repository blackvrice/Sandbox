#pragma once
// 콘솔 출력 인코딩. 도구·서버의 메시지는 UTF-8(한국어)이다.
// Windows 콘솔(PowerShell·cmd)의 기본 코드 페이지는 cp949 라서 그대로 쓰면 글자가 깨진다 → 출력 코드 페이지를
// UTF-8(65001)로 바꾼다. 다른 OS 에서는 아무것도 하지 않는다. 프로세스 시작 때 한 번 부른다.

namespace sbx::console {

void useUtf8Output() noexcept;

} // namespace sbx::console
