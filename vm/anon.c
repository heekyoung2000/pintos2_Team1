/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"


/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	// swap_disk = NULL;
	swap_disk = disk_get(1,1); // swap용 디스크를 swap_disk에 저장한다.
	list_init(&swap_table);
	lock_init(&swap_table_lock);

	//swap_disk 크기만큼 slot을 만들어서 swap_table에 넣어둔다.
	//1 slot에 1 page를 담을 수 있는 slot 개수 구하기
	//: 1 sector = 512 bytes, 1 page = 4096bytes -> 1 slot = 8 sector
	disk_sector_t swap_size = disk_size(swap_disk) / 8;
	for(disk_sector_t i=0; i< swap_size;i++){
		struct slot *slot = (struct slot *)malloc(sizeof(struct slot));
		slot->page = NULL;
		slot->slot_no = i;
		lock_acquire(&swap_table_lock);
		list_push_back(&swap_table, &slot->swap_elem);
		lock_release(&swap_table_lock);
	}
}

/* Initialize the file mapping */
/*익명 페이지의 초기화를 수행하고 페이지의 핸들러를 설정하는 역할*/
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page ->slot_no =-1; //초기화함수가 호출되는 시점은 page가 매핑된 상태이므로 swap_slot을 차지하지 않는다.
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	disk_sector_t page_slot_no = anon_page ->slot_no; //swap_out된 페이지는 디스크 스왑 영역 anon_page 구조체 안에 저장되어있음
	struct list_elem *e;
	struct slot *slot;
	lock_acquire(&swap_table_lock);
	for(e=list_begin(&swap_table); e != list_end(&swap_table); e = list_next(e)){
		slot = list_entry(e, struct slot, swap_elem);
		if(slot->slot_no == page_slot_no){ //현재 page가 사용중인 slot 찾기
			for(int i=0; i<8; i++){ // 디스크, 읽을 섹터번호, 담을 주소를 읽는다.
				disk_read(swap_disk,page_slot_no*8 + i, kva+DISK_SECTOR_SIZE*i);

			}
			slot->page =NULL; // 빈 slot으로 업데이트 한다.
			anon_page -> slot_no =-1; //이 page는 swap_slot을 차지하지 않는다.
			lock_release(&swap_table_lock);
			return true;
		}
	}
	lock_release(&swap_table_lock);
	return false;

}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	if(page==NULL){
		return false;
	}
	struct anon_page *anon_page = &page->anon;
	struct list_elem *e;
	struct slot *slot;
	lock_acquire(&swap_table_lock);
	for(e = list_begin(&swap_table); e!=list_end(&swap_table); e =list_next(e)){
		slot = list_entry(e,struct slot, swap_elem);
		if(slot->page == NULL){
			for(int i=0;i<8;i++){
				disk_write(swap_disk,slot->slot_no*8 +i, page->va +DISK_SECTOR_SIZE *i);
			}
			anon_page->slot_no = slot->slot_no;
			slot->page = page;
			//page와 frame의 연결을 끊는다.
			page->frame->page= NULL;
			page->frame =NULL;
			pml4_clear_page(thread_current()->pml4, page->va);
			lock_release(&swap_table_lock);
			return true;
		}
	}
	lock_release(&swap_table_lock);
	PANIC("insufficent swap space");
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	struct list_elem *e;
	struct slot *slot;

	lock_acquire(&swap_table_lock);
	for(e=list_begin(&swap_table); e!=list_end(&swap_table); e= list_next(e)){
		slot= list_entry(e, struct slot, swap_elem);
		if(slot->slot_no == anon_page->slot_no){
			slot->page =NULL; // 페이지 리소스 해제
			break;
		}
	}
	lock_release(&swap_table_lock);

}
