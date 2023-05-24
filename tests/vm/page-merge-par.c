#include "tests/main.h"
#include "tests/vm/parallel-merge.h"

void
test_main (void) 
{
  parallel_merge ("child-sort", 123);
}
/*exit_status가 123이면 프로세스가 정상적으로 종료되었지만 비정상적인 조건으로 인해 종료되었음을 의미합니다. 이는 프로세스가 충돌하거나 
  리소스를 사용할 수 없거나 명령줄 인수가 잘못되었음을 의미할 수 있습니다.*/