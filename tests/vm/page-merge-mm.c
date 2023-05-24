#include "tests/main.h"
#include "tests/vm/parallel-merge.h"

void
test_main (void) 
{
  parallel_merge ("child-qsort-mm", 80);
}

/*exit_status가 80이면 프로세스가 실패했음을 의미합니다. 이는 프로세스가 명령줄 인수를 처리하지 못하거나 올바르게 실행할 수 없는 리소스를 찾지 못하거나 충돌했음을 
  의미할 수 있습니다.
*/