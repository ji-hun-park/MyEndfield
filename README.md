**"Vulkan 기반 자체 렌더 파이프라인 엔진"**

![그림01](https://ji-hun-park.github.io/assets/images/엔드필드001.png "그림01")
![그림02](https://ji-hun-park.github.io/assets/images/엔드필드002.png "그림02")
**별도의 창에서 렌더링되며 움직이는 만 개의 고밀도 모델들**

## 0. Links
📥**Download:** []()  
🎬**Explanation Video:** [Project Explanation Video](https://youtu.be/ycOyzvgSdMk?si=GY3JUIKaq-qP8HgB)   

## 1. 프로젝트 개요
> <명일방주: 엔드필드>의 ECS 및 렌더링 파이프라인을 모방한 **세미 엔진 프로젝트**입니다.  
> 상용 엔진의 파이프라인이 아닌 독자적인 파이프라인을 통해 렌더링 과정을 최적화하는 연습을 했습니다.

Unite Seoul 2026에서 들은 HyperGryph의 Tech Lead이신 Will Fei님의 강연 내용을 토대로  
실제 명일방주: 엔드필드의 파이프라인 구조를 모방해보는 경험을 해봤습니다.

**시스템 흐름도**  
![시스템 흐름도](https://ji-hun-park.github.io/assets/images/엔드필드000.png "Game Logic Flow")

### 📋프로젝트 정보

* **장르 / 플랫폼:** 오픈월드 게임 / PC (Single)
* **개발 인원:** 1인 개발
* **개발 기간:** 2026.10~2026.11 약 2개월 소요
* **사용 엔진 / 언어 / 환경:** Unity6.6 (6000.6.9f1) / C#, C++(Vulkan) / VSCode, agy

## 2. 게임 소개
**[Unity 자체 ECS와 별도 자체 파이프라인]**  
오브젝트들이 자체 ECS를 통해 관리됩니다.  
Unity Editor에서 모델의 정보를 가져와 DLL 플러그인과 Native Render Feature를 이용해  
렌더 루프를 가로채 별도의 Window에서 실행하게 했습니다.  
독자적인 Vulkan 기반 렌더 파이프라인을 통해 렌더링됩니다.  
대규모 고밀도 모델 이동 시에도 프레임이 안정적으로 유지됩니다.

### 사용 에셋
실제 엔드필드의 레바테인 모델 FBX

## 3. 플레이 방식
유니티 에디터에서 씬 로더를 통해 오브젝트들의 정보를 가져옵니다.  
이후 별도의 창에서 모델들이 렌더링 됩니다.  
테스트용으로 10,000개의 모델들이 스폰된 후 각자 위아래로 반복 이동합니다.

## 4. 핵심 구현 내용
1.렌더링 아키텍처 코어 (C++ Native Pipeline)
- 주요 규칙은 고수준 API 의존 탈피 및 네이티브 C++ 파이프라인 통제로 C++에서 온전히 루프를 소유해야 한다입니다.

Unity 엔진은 보통 윈도우의 Directx11에서 구동이 됩니다.  
이를 Vulkan으로 전환하는 기능과 이를 위한 인터페이스들이 존재하지만  
저는 자체적으로 개발했습니다.

![그림03](https://ji-hun-park.github.io/assets/images/엔드필드003.png "그림03")
![그림04](https://ji-hun-park.github.io/assets/images/엔드필드004.png "그림04")
**자체 Vulkan 파이프라인 생성 구조**

DLL Plugin을 통해 제가 자체 제작한 Custum Vulkan이 탑재된  
C++코드로 구동하도록 중간에 가로챈 뒤  
PC_Render에 Native Render Feature를 추가해  
별도의 Windows 창을 띄워 그 곳에서 렌더링되도록 변경했습니다.

그리고 현재 Unity 엔진에선 Entity 정보,  
Component(위치)와 Mesh 정보를 제공하는 기능만 수행하게 했습니다.  
이를 통해 Unity에서 주로 쓰이는 언어인 C#의 관리형 언어의  
오버헤드를 줄일 수 있었습니다.

오류 메시지나 알림 등 각종 로그 확인은  
C++에서 콜백 함수를 통해 Plugin을 거쳐  
Unity Editor에 알려준 뒤 콘솔에 출력하도록 설계했습니다.

2.커스텀 ECS 및 데이터 지향 설계(Data-Oriented Design)
- 주요 규칙은 자체 ECS 구축, 128-bit 고정 마스크 사용, SoA Chunk Memory 사용, 포인터 체이싱 배제하기입니다.

Unity에선 이미 자체적으로 ECS를 지원하기 위한 DOTS 패키지가 있습니다.  
또한 Burst Compiler와 Job System을 통해  
CPU SIMD 최적화를 구현하기 쉽도록 기능을 제공합니다.

그러나 강연의 내용을 그대로 따라하기 위해 해당 기능들을 사용하지 않고  
별도의 ECS를 자체적으로 만들고 메모리 구조 역시 자체적으로 구축했습니다.  
128-bit의 크기에 맞춰 ComponentMask를 uint64_t low, high로 설계했습니다.  
Chunk 클래스 내에서 columns를 통해 SoA(Structure of Array) 방식으로 메모리를 연속 할당하도록 구현했습니다.

Component Query 시 리플렉션이나 딕셔너리 없이  
QueryChunks 내부에서 mask.Contains() 함수를 통해  
단순 비트 연산만으로 필터링하도록 했습니다.

삭제 시 Swap-and-Pop을 통해 메모리 단편화를 방지하고 O(1) time에 처리하는 것을 보장하게 했습니다.  
또한 극단적인 캐시 최적화를 위해 엔티티 핸들자체(8-byte)에 인덱스를 버저닝하여 맵 조회를 없앴습니다.

3.멀티스레딩 및 컬링
- 주요 규칙은 Lock-Free 병렬화, 타일 기반 래스터라이즈, 단일 컬링 패스

Culling.cpp에서 RasterizeTilesParallel 함수는 화면을 8개의 타일로 쪼개고, 4개의 워커 스레드가 각자 타일을 맡도록 구현했습니다.  
이 때 오브젝트 역시 8개로 쪼갠 뒤 각자 자신이 맡은 구역에 존재하는 오브젝트 조각만 작업을 담당하게 했습니다.  
이렇게 하면 서로가 자원을 공유할 일이 전혀 없기 때문에 Lock이나 Mutex 등이 필요없어집니다.  
그렇기에 Lock-Free 구현을 통해 쓸데없는 Hanging이 사라진 상태로 4개의 워커 스레드가 동시에 작업을 진행하기 때문에 굉장히 빠른 컬링을 수행합니다.

4.그래픽스 리소스 관리 및 배리어 최적화
- 주요 규칙은 64-bit 단일 정렬 키, Dynamic Offset, 배리어 병합(Render Graph)입니다.

우선 Sort Key를 구현해서 uint64_t union을 활용해 총 64비트를  
passID(16비트) -> pipelineID(16비트) -> materialID(16비트) -> depth(16비트) 의 구조로  
순서대로 나누어서 하나의 키에 4가지를 한꺼번에 집어넣었습니다.  
Key를 이런 구조로 구성한다면 std::sort 만으로도 배칭 정렬이 완료되어 효율적입니다.  
아래는 기수 정렬과 비교한 상세한 설명이 담긴 게시글입니다.  
**Radix Sort VS std::sort** [DeepLink](https://ji-hun-park.github.io/myendfield/simd-sortkey-vs-radix-sort/)

다음으로 동적 오프셋 할당을 위해 VulkanBackend.cpp의 디스크립터 레이아웃을  
0번 - 패스당 1회 (라이팅 / 카메라 / 그림자)  
1번 - 머터리얼당 (텍스처 / 상수, 해시 기반 캐싱)  
2번 - 드로우당 (모델 매트릭스 등 큰 버퍼 하나 + 동적 오프셋으로 처리, 엔티티당 비용 최소화) 으로 분류했으며

중복 오프셋 방지를 위해 0xF7F7F7F7 매직을 사용했습니다.  
이 마법의 원리는 이 값은 선언될 일이 없으므로 병렬 빌드 시 이전과 이후의 값을 저장하지 못하는데  
이 마법을 쓰면 전이가 일어났다면 값이 변했을 것이고 아니면 그대로란 점을 이용할 수 있습니다.  
저의 경우 디스크립터 레이아웃 2번(오브젝트)을  
VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC으로 선언했으며  
프레임마다 별도의 메모리 할당 없이 하나의 거대한 버퍼에서 동적 오프셋만 이동하도록 설계했습니다.

마지막으로 Render Graph와 배리어 병합을 위해  
RenderGraph.h / .cpp에서 각 패스의 자원 사용을 AccessTag로 선언하게 하고  
CompileGraph() 호출 시 컴파일 타임에 접근 마스크를 모아서  
하나의 파이프라인 배리어 그룹(MergedBarrierGroup)으로 병합하는 구조를 구현했습니다.

5.테스트 및 벤치마크

![그림05](https://ji-hun-park.github.io/assets/images/엔드필드005.png "그림05")

그 밖에 여러 예외 처리나 검증을 위해 Google Test를 통해 단위 테스트도 진행을 했습니다.

![그림06](https://ji-hun-park.github.io/assets/images/엔드필드006.png "그림06")

최종적으로 스포너를 통해 10,000 개의 FBX(각 서브 메쉬 19개로 구성)를 스폰해 프레임 안정적으로 렌더링하는데 성공했습니다.  
이는 총 19만 개의 메시와 폴리곤(삼각형) 약 21억 개, 정점 약 18억 개로 이루어져 있습니다.

![그림07](https://ji-hun-park.github.io/assets/images/엔드필드007.png "그림07")

![그림08](https://ji-hun-park.github.io/assets/images/엔드필드008.png "그림08")

또한 성능 비교를 위해 현재 버전과 Unity DOTS, Burst, Job 시스템을 이용하고  
유니티에서 자체 지원하는 Vulkan API를 이용하는 버전과 그냥 OOP 및 URP와 GPU Instancing을 사용하는 버전을  
준비해 자체 벤치마크도 진행했습니다.  
환경은 Unity Editor에서 진행했으며, Vulkan의 경우 DebugMessenger를 거치게 됩니다.  
CPU는 AMD Ryzen 7 7700, GPU는 NVIDIA RTX 3070 입니다.  
그 결과 외부 프로그램이 켜져있는 상태에서  
OOP 및 URP와 GPU Instancing을 사용하는 버전 대비 약 6배의 평균 프레임 보존율을 기록했습니다.

## 5. 트러블 슈팅
### 좌표계 정합 문제
- 문제: 캐릭터가 반대로 뒤집히는 문제
- 원인: Unity(Y-Up)와 Vulkan(Y-Down) 간 좌표계 불일치로 인한 정점 감기 순서 역전 결함
- 해결: 투영 행렬 Y축 스케일 부호 반전으로 해결  

### 캐릭터 서브메시 분리 문제
- 문제: 캐릭터들이 서브메시 단위로 분해되어 따로 움직이는 문제
- 원인: 서브메시가 같은 그룹으로 묶이지 않아 서로 위상이 달랐음
- 해결: 서브메시 개수로 그룹화 및 동일한 위상 오프셋을 부여해 해결

### WAR Hazard 문제
- 문제: 화면이 깜빡임
- 원인: 멀티스레드 행렬 복사 시 경합 문제
- 해결: CAS 원자적 오프셋 확보 및 버퍼 구간 물리적 분리로 해결

### 유니티 잡 시스템과 윈도우 OS 간 충돌 문제
- 문제: 화면에 캐릭터가 렌더링되지 않음
- 원인: 별도 창 생성시 콜백 함수들이 유니티의 여러 워커 스레드 중 하나에 무작위로 할당되어 비동기로 실행돼 윈도우 OS가 메시지 응답 부재로 메시지 펌프를 멈추는 현상
- 해결: 윈도우 창 관리 전용 백그라운드 스레드 별도 분리 생성으로 해결
