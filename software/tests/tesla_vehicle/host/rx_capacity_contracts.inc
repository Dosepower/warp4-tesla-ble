// H1: RX queue capacity versus notification fragmentation (production enqueue_rx,
// production RxFragment/RX_QUEUE_DEPTH, FreeRTOS-shaped HostQueue with enforced
// capacity, real tesla_clean::FrameReceiver). Deterministic; no RF recording.
//
// Independent test-side contract (deliberately NOT read from production):
//  - every notification payload 1..512 is admitted while capacity remains
//    (nothing valid is filtered); one notification costs ceil(len/chunk) slots
//    and is admitted all-or-nothing;
//  - GUARANTEE without any consumer progress (one synchronous submit or one
//    scheduler yield): any notification sequence of at most 1282 stream bytes
//    (one maximum framed message 1024+2 plus 256 accompanying bytes) whose
//    notifications carry >= 20 bytes (ATT minimum MTU 23 - 3), except at most
//    5 shorter ones (deferred_[4] complete frames + one frame in progress);
//  - tiny notifications are supported, but only as many as there are slots;
//    no unbounded notification count is promised. Overflow = fail-safe abort.
static constexpr size_t RX_CONTRACT_FULL_MIN = 20, RX_CONTRACT_BYTES = 1024 + 2 + 256, RX_CONTRACT_SHORT = 5;
static size_t rx_cost(size_t len) { return (len + TeslaVehicle::RX_CHUNK_BYTES - 1) / TeslaVehicle::RX_CHUNK_BYTES; }
static std::vector<std::vector<uint8_t>> rx_split(const std::vector<uint8_t> &all, size_t payload) {
 std::vector<std::vector<uint8_t>> out;for(size_t i=0;i<all.size();i+=payload)out.emplace_back(all.begin()+i,all.begin()+std::min(all.size(),i+payload));return out;
}
static std::vector<uint8_t> rx_frame_bytes(size_t length, uint8_t seed) {
 std::vector<uint8_t> f{uint8_t(length>>8),uint8_t(length)};for(size_t i=0;i<length;i++)f.push_back(uint8_t(seed+i*7));return f;
}
// Enqueue a whole burst without any consumer progress. Returns admitted
// notification count; on the first refusal asserts that the queue is unchanged
// (no partial prefix) and stops, like the production session invalidation.
static size_t rx_offer(QueueHandle_t q, const std::vector<std::vector<uint8_t>> &burst, uint64_t generation=7) {
 size_t admitted=0;
 for(const auto &n:burst){
  const size_t before=uxQueueSpacesAvailable(q);uint32_t peak=0;
  if(!TeslaVehicle::enqueue_rx(q,generation,n.data(),n.size(),&peak)){assert(uxQueueSpacesAvailable(q)==before&&"refused notification must not leave a partial prefix");return admitted;}
  assert(uxQueueSpacesAvailable(q)==before-rx_cost(n.size()));
  assert(peak==TeslaVehicle::RX_QUEUE_DEPTH-uxQueueSpacesAvailable(q));
  ++admitted;
 }
 return admitted;
}
// Drain like the worker and reassemble with the real FrameReceiver; returns frames.
static std::vector<std::vector<uint8_t>> rx_reassemble(QueueHandle_t q, uint64_t generation=7) {
 tesla_clean::FrameReceiver receiver;receiver.reset(generation);TeslaVehicle::RxFragment f;uint32_t now=100;
 std::vector<std::vector<uint8_t>> frames;
 while(xQueueReceive(q,&f,0)==pdTRUE){
  assert(f.connection_generation==generation&&f.length>0&&f.length<=TeslaVehicle::RX_CHUNK_BYTES);
  assert(receiver.receive({f.data,f.length},now,generation,[](void*p,const tesla_clean::Frame &v){static_cast<std::vector<std::vector<uint8_t>>*>(p)->emplace_back(v.data.begin(),v.data.begin()+v.length);},&frames));
 }
 return frames;
}
static void rx_capacity_units() {
 const size_t depth=TeslaVehicle::RX_QUEUE_DEPTH,chunk=TeslaVehicle::RX_CHUNK_BYTES;
 assert(TeslaVehicle::RX_NOTIFICATION_MAX==512&&sizeof(TeslaVehicle::RxFragment)==32&&chunk==22);
 // 1. Exact worst case of the contract (DP over every admissible mixture).
 std::vector<long> best(RX_CONTRACT_BYTES+1,-1);best[0]=0;
 for(size_t s=1;s<=RX_CONTRACT_BYTES;s++)for(size_t p=RX_CONTRACT_FULL_MIN;p<=std::min<size_t>(512,s);p++)if(best[s-p]>=0)best[s]=std::max(best[s],best[s-p]+long(rx_cost(p)));
 size_t worst=0,worst_k=0,worst_s=0;
 for(size_t k=0;k<=RX_CONTRACT_SHORT;k++)for(size_t s=0;s+k<=RX_CONTRACT_BYTES;s++)if(best[s]>=0&&size_t(best[s])+k>worst){worst=size_t(best[s])+k;worst_k=k;worst_s=s;}
 std::printf("RX_CAPACITY contract_bytes=%zu full_min=%zu short=%zu exact_worst_slots=%zu (full_bytes=%zu short=%zu) depth=%zu\n",RX_CONTRACT_BYTES,RX_CONTRACT_FULL_MIN,RX_CONTRACT_SHORT,worst,worst_s,worst_k,depth);
 auto q=xQueueCreate(depth,sizeof(TeslaVehicle::RxFragment));
 {  // Build the DP argmax adversarial burst and actually offer it.
  std::vector<std::vector<uint8_t>> burst;size_t s=worst_s;
  while(s){for(size_t p=RX_CONTRACT_FULL_MIN;p<=std::min<size_t>(512,s);p++)if(best[s-p]>=0&&best[s]==best[s-p]+long(rx_cost(p))){burst.emplace_back(p,uint8_t(p));s-=p;break;}}
  for(size_t k=0;k<worst_k;k++)burst.emplace_back(1,uint8_t(0xa0+k));
  const size_t admitted=rx_offer(q,burst);
  if(admitted!=burst.size())std::fprintf(stderr,"RX_CAPACITY_RED adversarial contract burst refused after %zu/%zu notifications (needs %zu slots, depth %zu)\n",admitted,burst.size(),worst,depth);
  assert(worst<=depth&&admitted==burst.size()&&"contract worst case must fit without consumer progress");
  xQueueReset(q);
 }
 // 2. Every notification size 1..512, one maximum framed message, no consumer.
 const auto max_frame=rx_frame_bytes(1024,3);
 size_t first_unguaranteed=0;
 for(size_t p=1;p<=512;p++){
  xQueueReset(q);const auto burst=rx_split(max_frame,p);size_t need=0;for(auto&n:burst)need+=rx_cost(n.size());
  const size_t admitted=rx_offer(q,burst);
  assert((admitted==burst.size())==(need<=depth));
  if(p>=RX_CONTRACT_FULL_MIN){
   if(admitted!=burst.size()){std::fprintf(stderr,"RX_CAPACITY_RED max frame at notification payload %zu needs %zu slots > depth %zu (notifications=%zu, refused at %zu)\n",p,need,depth,burst.size(),admitted);assert(false&&"maximum frame at a guaranteed notification payload refused");}
   const auto frames=rx_reassemble(q);assert(frames.size()==1&&frames[0]==std::vector<uint8_t>(max_frame.begin()+2,max_frame.end()));
  } else if(admitted!=burst.size()&&!first_unguaranteed)first_unguaranteed=p;
 }
 // 3. Guaranteed window with three separately framed messages (1026+6+250 = 1282
 //    bytes), fragmented per frame (three short tails) and as one stream.
 const auto wait=rx_frame_bytes(4,0x22),extra=rx_frame_bytes(248,0x55);
 std::vector<uint8_t> stream=max_frame;stream.insert(stream.end(),wait.begin(),wait.end());stream.insert(stream.end(),extra.begin(),extra.end());
 assert(stream.size()==RX_CONTRACT_BYTES);
 size_t max_slots=0,max_slots_p=0;
 for(size_t p=RX_CONTRACT_FULL_MIN;p<=512;p++)for(int per_frame=0;per_frame<2;per_frame++){
  xQueueReset(q);std::vector<std::vector<uint8_t>> burst;
  if(per_frame){for(const auto*f:{&max_frame,&wait,&extra})for(auto&n:rx_split(*f,p))burst.push_back(n);}else burst=rx_split(stream,p);
  size_t need=0,shorter=0;for(auto&n:burst){need+=rx_cost(n.size());shorter+=n.size()<RX_CONTRACT_FULL_MIN;}
  assert(shorter<=RX_CONTRACT_SHORT);if(need>max_slots){max_slots=need;max_slots_p=p;}
  if(rx_offer(q,burst)!=burst.size()){std::fprintf(stderr,"RX_CAPACITY_RED guaranteed 3-frame window refused: payload=%zu per_frame=%d slots=%zu depth=%zu\n",p,per_frame,need,depth);assert(false);}
  const auto frames=rx_reassemble(q);assert(frames.size()==3&&frames[0].size()==1024&&frames[1].size()==4&&frames[2].size()==248);
 }
 // 4. Tiny notifications: exactly depth one-byte notifications, then refusal;
 //    a consumed slot admits the next one. Explicitly bounded, not unbounded.
 xQueueReset(q);uint8_t one=0x42;
 for(size_t i=0;i<depth;i++)assert(TeslaVehicle::enqueue_rx(q,7,&one,1));
 assert(uxQueueSpacesAvailable(q)==0&&!TeslaVehicle::enqueue_rx(q,7,&one,1)&&uxQueueSpacesAvailable(q)==0);
 {TeslaVehicle::RxFragment f;assert(xQueueReceive(q,&f,0)==pdTRUE);}assert(TeslaVehicle::enqueue_rx(q,7,&one,1));
 // 5. All-or-nothing at the last free slot: 23 bytes (2 slots) refused, 22 admitted.
 xQueueReset(q);for(size_t i=0;i+1<depth;i++)assert(TeslaVehicle::enqueue_rx(q,7,&one,1));
 std::vector<uint8_t> b23(23,0x99);assert(!TeslaVehicle::enqueue_rx(q,7,b23.data(),23)&&uxQueueSpacesAvailable(q)==1);
 {size_t n=0;TeslaVehicle::RxFragment f;std::deque<TeslaVehicle::RxFragment> kept;while(xQueueReceive(q,&f,0)==pdTRUE){assert(f.length==1&&f.data[0]==0x42);kept.push_back(f);++n;}assert(n==depth-1);for(auto&k:kept)assert(xQueueSend(q,&k,0)==pdTRUE);}
 assert(TeslaVehicle::enqueue_rx(q,7,b23.data(),22)&&uxQueueSpacesAvailable(q)==0);
 // 6. Callback bound: 512 admitted as ceil(512/22)=24 slots; 513/0/null refused without consumption.
 xQueueReset(q);std::vector<uint8_t> b513(513,1);
 assert(!TeslaVehicle::enqueue_rx(q,7,b513.data(),513)&&!TeslaVehicle::enqueue_rx(q,7,b513.data(),0)&&!TeslaVehicle::enqueue_rx(q,7,nullptr,5)&&uxQueueSpacesAvailable(q)==depth);
 assert(TeslaVehicle::enqueue_rx(q,7,b513.data(),512)&&uxQueueSpacesAvailable(q)==depth-24);
 // 7. Two maximum frames in one window exceed the contract: explicit fail-safe refusal.
 xQueueReset(q);{auto two=max_frame;two.insert(two.end(),max_frame.begin(),max_frame.end());const auto burst=rx_split(two,23);assert(rx_offer(q,burst)<burst.size());}
 vQueueDelete(q);
 std::printf("RX_CAPACITY_UNITS PASS depth=%zu element=%zu queue_data_bytes=%zu exact_worst_slots=%zu guaranteed_window_max_slots=%zu@payload%zu first_refused_tiny_payload=%zu\n",depth,sizeof(TeslaVehicle::RxFragment),depth*sizeof(TeslaVehicle::RxFragment),worst,max_slots,max_slots_p,first_unguaranteed);
}
