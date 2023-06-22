/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include "threads/thread.h"


/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`.
 * 만약 페이지를 생성하고자 한다면, 직접 생성하지 말고 vm_alloc_page 함수나 제공된 초기화자를 사용해야 합니다. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) { //vm_alloc_page_with_initializer (VM_ANON, upage,writable, lazy_load_segment, lazy_load_arg), 
		// setup stack - vm_alloc_page_with_initializer(VM_ANON | VM_MARKER_0 ,stack_bottom,1, NULL,NULL)
		// vm_stack_growth -vm_alloc_page(VM_ANON|VM_MARKER_0,pg_round_down(addr),1);

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	//upage가 이미 사용중인지 확인한다.
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
		/*페이지를 생성하고, 해당 가상 메모리 유형에 맞는 초기화기를 가져옵니다.
	uninit_new를 호출하여 'uninit' 페이지 구조체를 생성합니다. 이때, 초기화기에 따라 필드를 수정해야 합니다.
	생성된 페이지를 supplemental_page_table (spt)에 삽입합니다.*/
		struct page *p = (struct page *)malloc(sizeof(struct page)); //페이지 생성
		bool(*page_initializer)(struct page*,enum vm_type,void *); // 포인터를 나타냄

		switch(VM_TYPE(type)){
			case VM_ANON:
				page_initializer = anon_initializer;
				break;
			case VM_FILE:
				page_initializer = file_backed_initializer;
				break;
		}

		uninit_new(p,upage,init,type,aux,page_initializer);//uninit_new 호출해서 "uninit" 페이지 구조체를 생성

		p->writable = writable; //page의 상태를 수정 가능한 상태로 만듦

		return spt_insert_page(spt,p); // 페이지를 만들었으므로 spt에 페이지 삽입
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page;
	page = (struct page *)malloc(sizeof(struct page));
	struct hash_elem *e;

	page->va = pg_round_down(va); //해당 va가 속해있는 페이지 시작 주소를 갖는 page를 만든다.
	/* 주어진 가상 주소를 페이지 경계로 정렬함으로써 해당 가상 주소가 속한 페이지의 시작 주소를 구할 수 있습니다. 
	이는 페이지 테이블이나 보조 페이지 테이블과 같은 자료 구조에서 페이지를 검색하거나 관리하는 데 사용됩니다*/
	/* e와 같은 해시값을 갖는 page를 spt에서 찾은 다음 해당 hash_elem을 리턴*/
	e = hash_find(&spt->spt_hash, &page->hash_elem);
	free(page);

	if(e!= NULL){
		return hash_entry(e,struct page,hash_elem);
	}
	else{
		return NULL;
	}

	/* TODO: Fill this function. */
	/* 주어진 가상 주소(va)를 사용하여 보조 테이블안에서 해당 페이지를 찾으려고 시도*/
	// hash_find (struct hash *h, struct hash_elem *e)
	// return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	// int succ = false;
	// /* TODO: Fill this function. */
	// struct hash_elem *e;

	// hash_insert(&spt,&page -> hash_elem);
	// //삽입하려는 가상 주소가 이미 보조 페이지 테이블에 등록되어 있는 경우
	// if(spt_find_page!=NULL){
	// 	return succ;
	// 	//중복된 매핑을 허용하지 않기 위해 이를 체크해서 삽입 중단 또는 오류 처리
	// }
	// else{
	// 	return true;
	// }

	return hash_insert(&spt->spt_hash,&page->hash_elem)==NULL? true:false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.
 * palloc()은 사용 가능한 페이지를 할당하고 해당 프레임을 반환하는 함수입니다. 
 * 사용 가능한 페이지가 없는 경우, 페이지를 대체(evict)하고 해당 페이지를 반환합니다. 
 * 이 함수는 항상 유효한 주소를 반환합니다. 즉, 사용자 풀 메모리가 가득 찬 경우, 이 함수는 사용 가능한 메모리 공간을 확보하기 위해 프레임을 대체합니다. 
 * 따라서 항상 유효한 메모리 주소를 반환합니다.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame;
	/* TODO: Fill this function. */
	void *kva = palloc_get_page(PAL_USER);

	if(kva==NULL) PANIC("todo");

	frame =(struct frame *)malloc(sizeof(struct frame));
	frame->kva = kva;
	frame->page = NULL;
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	vm_alloc_page(VM_ANON|VM_MARKER_0,pg_round_down(addr),1);
	
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if(addr ==NULL) return false; // 주소가 유효하지 않을 경우 false
	// return vm_do_claim_page (page);

	if(is_kernel_vaddr(addr)) return false; // 유저 스택이 아닐 경우(즉 커널 스택일 경우) 여기서 끝내기

	if(not_present){ // 접근한 메모리의 physical page가 존재하지 않는 경우= 물리메모리에 매핑이 되어 있지 않은 경우
		/*project 3 - stack growth*/
		void *rsp = f->rsp; // 이떄 f->rsp는 유저 스택을 가리킨다.
		if(!user) 
			rsp= thread_current()->rsp;// kernel access일 경우 thread 에서 rsp를 가져와야 한다.

		// 스택 확장으로 처리할 수 있는 폴트인 경우 vm_stack_growth를 호출
		if(USER_STACK - (1<<20) <= rsp-8 && rsp-8==addr && addr<= USER_STACK) 
			vm_stack_growth(addr);
		else if(USER_STACK - (1<<20)<= rsp && rsp <= addr && addr <=USER_STACK) 
			vm_stack_growth(addr);

		/*-------------------*/
		page = spt_find_page(spt,addr);
		if(page == NULL) 
			return false;
		if(write == 1 && page->writable ==0) 
			return false;

		return vm_do_claim_page(page);
	}
	return false;

	
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt,va);
	if(page == NULL) return false;
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
/*가상 메모리 관리 시스템에서 페이지 테이블 엔트리와 MMU를 설정하여 가상 주소를 실제 물리 주소에 매핑하는 과정을 수행*/
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links 페이지와 프레임 사이의 링크를 설정하는 과정*/
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *current = thread_current();
	pml4_set_page(current->pml4, page->va, frame->kva, page->writable);//spt가 아닌 pt에 kva를 저장

	
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	/* 1. 보조 테이블 함수 추가하기*/
	/* 2. 보조 테이블에 사용할 데이터 구조 선택하기*/
	// hash_init(&(spt),hash_hash_func,hash_less_func,NULL);
	hash_init(&spt->spt_hash,page_hash,page_less,NULL);
	/*bool
hash_init (struct hash *h,
		hash_hash_func *hash, hash_less_func *less, void *aux)*/


}

/* Copy supplemental page table from src to dst */
/*"src"로부터 "dst"로 보조 페이지 테이블을 복사하는 작업을 하는데 이때 src는 복사할 대상이 되는 보조 페이지 테이블,
dst는 복사된 결과가 저장될 보조 페이지 테이블 */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
			struct hash_iterator i;
			hash_first(&i, &src->spt_hash);
			while(hash_next(&i)){
				//src_page 정보
				struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
				enum vm_type type = src_page ->operations->type;
				void *upage = src_page -> va;
				bool writable = src_page->writable;

				/*1) type이 uninit이면 */
				if(type == VM_UNINIT){
					vm_initializer *init = src_page -> uninit.init;
					void *aux = src_page->uninit.aux;
					vm_alloc_page_with_initializer(VM_ANON,upage,writable,init,aux);
					continue;
				}

				/*2) type이 uninit이 아니면*/
				if(!vm_alloc_page_with_initializer(type,upage,writable,NULL,NULL)) return false;

				if(!vm_claim_page(upage)) return false;

				struct page *dst_page = spt_find_page(dst,upage);
				memcpy(dst_page->frame->kva,src_page->frame->kva,PGSIZE);
			}
			return true;
}

/* 페이지 구조체에 메모리 해제 작업을 수행*/
void hash_page_destroy(struct hash_elem *e, void *aux){
	struct page *page =hash_entry(e,struct page, hash_elem);
	destroy(page);
	free(page);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	/*페이지 테이블과 관련된 자원만 해제, 해시 테이블은 그대로 보존 */
	// hash_destroy(&spt->spt_hash,hash_page_destroy); // 해시 테이블에서 모든 요소를 제거 후 테이블까지 파괴
	hash_clear(&spt->spt_hash,hash_page_destroy); // 해시 테이블에서 모든 요소를 제거
}

/*project3 새로 푸가*/
/* 페이지의 해시 값을 계산하는 역할을 하는 함수
	페이지의 속성을 기반으로 해시 값을 계산하여 반환한다.
	hash_elem의 멤버인 페이지를 매개변수로 받아 해시 값을 계산하고 반환한다.*/
unsigned page_hash(const struct hash_elem *e, void *aux UNUSED){
	const struct page *p = hash_entry(e,struct page,hash_elem);
	return hash_bytes(&p -> va, sizeof p->va);

}
/* 두페이지의 순서를 비교하는 역할을 하는 함수
	주어진 두 페이지를 비교해서 첫번째 페이지가 두 번째 페이지보다 작은지 여부를 반환한다.
*/
unsigned page_less(const struct hash_elem *a,
		const struct hash_elem *b,
		void *aux UNUSED){

			const struct page *a_ = hash_entry(a, struct page, hash_elem);
			const struct page *b_ = hash_entry(b, struct page, hash_elem);

			return a_->va < b_->va;

}