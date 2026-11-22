# 플레이어 간 교환 · 그냥 주기 · 거래

구현: `Content/Transfer/TransferHelper.cpp`, `TransferPlan.h`, `Protocol/PlayerTransfer.h`.
클라이언트: 인접 HARVEN-CLIENT의 `native/src/ui/game/transfer_ui.cpp`와 `game/features/transfer/transfer_packets.gd`.

| 기능 | 요청자 | 상대 | 조건 |
| --- | --- | --- | --- |
| 교환 | 물품 제안 | 물품 제안 | 양쪽 물품 필요, 돈 없음 |
| 그냥 주기 | 물품 제공 | 수령 동의 | 대가 없음 |
| 거래 | 물품과 총가격 제시 | 그로시 지불 | 가격 1 이상, 구매자 확인 후 판매자 확정 |

채팅의 플레이어 이름 메뉴에서 요청한다. 상대 수락 → 물품·수량/가격 등록 → 양쪽 준비 → 내용 확인 → 최종 확정.
물품 등록은 최대 8칸이며 일부 수량을 전달할 수 있다. 조건이 바뀌면 양쪽 준비와 확인을 모두 초기화한다.
장비 강화(32비트)·옵션을 보존하고 전달된 장비에 거래 이력 bit 9를 설정한다. 귀속·착용 장비는 등록할 수 없다.
클라이언트 가방은 서버 v3의 50칸을 두 페이지로 표시한다.

같은 Zone 내 Chebyshev 거리 3칸 이하에서 가능하다. 확정 전에는 사망·로그아웃·Zone 변경·거리 이탈·시간 초과로 취소한다.
요청 만료 30초, 편집/확인 대기 120초, 요청 간격 2초. 환경 변수 `HARVEN_TRANSFER_INVITE_MS`,
`HARVEN_TRANSFER_EDIT_MS`, `HARVEN_TRANSFER_REQUEST_MS`, `HARVEN_TRANSFER_DISTANCE`로 조정할 수 있다.
확정 저장 중에는 슬롯·대금과 이동을 잠근다. 접속을 끊어도 계정을 보존하고 DB 결과를 처리한 다음 로그아웃한다.

DB 두 샤드의 이전 저장을 배출한 뒤 한 커넥션에서 두 사용자를 ID 오름차순으로 잠그고 원자적으로 저장한다.
응답 유실은 같은 receipt로 재시도하며 중복 반영되지 않는다. 확정된 작업은 DB가 기준이고 다음 접속에서 복구한다.
운영 적용 전 `Shared/Database/mysql/migrate_player_transfer.sql`을 적용하고 `schema_verify.sql`을 확인한다.
서버 배포는 아직 진행하지 않았다. 개발 RDS 적용은 아래 후속 작업 기록을 참고한다.

검증:

```sh
cmake -S . -B build/transfer-memory -DHARVEN_IN_MEMORY_DB=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/transfer-memory -j4
python3 Tools/Test/regress_transfer.py --server build/transfer-memory/GameServer
python3 Tools/Test/regress_transfer_mysql.py --runtime build/enchant-mysql/runtime --libs build/enchant-mysql/deps/usr/lib/x86_64-linux-gnu
```

클라이언트 CMake의 `HARVEN_TRANSFER_SHARED_DIR`는 이 서버의 Shared 디렉터리다. 기존 데이터 표는 고정 서버 헤더를 계속 사용한다.
현재 클라이언트의 external/server 체크아웃은 코드가 요구하는 `311b945`보다 오래됐고 로컬 지도 수정이 있다.
그 수정을 보존하기 위해 빌드 검증은 `311b945`를 `build/transfer-client-server`에 내보낸 사본으로 진행한다.
사본의 `Data/TownMap.h`, `TownTypes.h`, `WorldData.h`에는 기존 클라이언트의 수정본을 반영했고 원본 서브모듈은 변경하지 않았다.
클라이언트 빌드 시 `-DHARVEN_SERVER_DIR=<그 사본>`을 지정하거나 기존 수정 보존 후 필요한 고정 헤더 버전을 준비한다.

검증 결과(2026-10-13): 실제 TCP 15개 시나리오, MySQL 6개 시나리오, 기존 제작/분해/강화 등 8개 시나리오 통과.
Windows 교환 UI·로그인·파티 관련 5개 테스트/65개 검증 통과. 실제 Godot 4.7.2에서 세 창의 그리기와
패킷 라우터·Esc 취소 전송을 확인했으며, 스크립트 전체 import에도 오류가 없었다.
Godot 세션 테스트도 6개 통과했다. 창 검증은 클라이언트의 `game/tests/transfer_smoke.tscn`으로 재실행한다.
`Godot --headless --path <클라이언트>/game res://tests/transfer_smoke.tscn --quit-after 10`.
Linux 클라이언트 전체 회귀는 기존 오디오 PCM 지문 2개와 부하 중 지연 검사에서 실패했다. 로그인 버전 검사 실패는 v3로 수정했다.
Windows 전체 회귀는 테스트 실행 파일의 스택을 16 MiB로 늘려 완료했다. 118개 중 117개 통과, 1개 실패, 2개는 기본 설정상 제외됐다.
실패는 기존 `net_thread` 지연 검사다. 10초 제한 내 1,000개 중 805개만 수신했으며, 이후 990번째 지연값 접근도 실패했다.
신규 기능 관련 테스트는 모두 통과했다. Windows 디버그·릴리스 DLL 빌드와 메모리 DB·ODBC 서버 빌드를 완료했다.

클라우드 DB 적용(2026-10-13, 후속 요청):

- DBeaver에 저장된 HARVEN 개발 RDS의 `GameServerDB`에 관리자 계정으로 접속했다. TLS 인증서와 호스트를 검증했다.
- 사용자가 요청한 `schema_precheck.sql` → `setup.sql` → `schema_verify.sql`을 실행했다.
  기존 스키마는 UserID 전환이 끝난 11개 테이블·38개 프로시저였고 활성 게임 DB 연결은 없었다.
- 기존 전체 테이블 데이터와 프로시저 정의를 Git에서 제외된 `build/transfer-client/cloud-db/before-setup-*.sql`에 백업했다.
  백업에는 사용자 데이터가 포함되므로 공유하거나 Git에 추가하지 않는다.
- 적용 후 테이블 15개·프로시저 51개, 누락/이전 이름 0개, `ok = 1`을 확인했다.
  `PlayerTransfer`, `P_PLAYER_TRANSFER`, 결사 성장과 경제 1차도 반영됐다.
- 기존 11개 테이블을 기본 키 순으로 읽어 행 수와 전체 값의 SHA-256을 비교했으며 전부 일치했다.
  사용자 11,509행, 인벤토리 2,115행을 포함해 기존 데이터는 보존됐다.
- `harven_app`의 `GRANT EXECUTE ON GameServerDB.*`를 확인했고, 재사전점검의 `next_step`은 `DONE`이었다.
