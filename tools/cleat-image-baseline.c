// Integration probe for the same C ABI consumed by Wheelhouse.
// Usage: probe inprocess|daemon 'producer command' [existing-session-id]
// Daemon mode uses CLEAT_RUNTIME_DIR and daemon name wh-image-baseline.
#include <cleat_provider.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct Counts { size_t updates, resources, placements, lookups, bytes; uint64_t max_offset; uint32_t viewport; } Counts;
static bool bytes(void *ctx, const uint8_t *data, size_t len) { ((Counts *)ctx)->bytes += len; return true; }
static void poll_view(cleat_session *s, Counts *c) {
  cleat_session_poll(s);
  cleat_render_update u = {0};
  if(cleat_session_render_update(s, &u)) {
    c->updates++; c->viewport=u.viewport_kind; if(u.scrollback_offset_rows>c->max_offset)c->max_offset=u.scrollback_offset_rows; c->resources += u.image_resource_count; c->placements += u.image_placement_count;
    for(size_t i=0;i<u.image_resource_count;i++) {
      const cleat_image_resource *r=&u.image_resources[i];
      c->lookups += cleat_session_with_image_resource_data(s,r->image_id,r->generation,bytes,c);
    }
    cleat_session_mark_observed(s,u.render_generation);
    cleat_session_release_render_update(s,&u);
  }
}
static void report(const char *name, cleat_session *s, Counts c) {
  printf("%s state=%u role=%u updates=%zu resources=%zu placements=%zu successful_lookups=%zu bytes=%zu viewport=%u max_offset=%llu\n",
    name,cleat_session_connection_state(s),cleat_session_role(s),c.updates,c.resources,c.placements,c.lookups,c.bytes,c.viewport,(unsigned long long)c.max_offset);
}
int main(int argc,char **argv) {
  if(argc<3) return 2;
  int daemon=!strcmp(argv[1],"daemon");
  cleat_provider_desc pd={.abi_version=CLEAT_PROVIDER_ABI_VERSION,.requested_features=CLEAT_PROVIDER_FEATURE_RENDER_UPDATES|CLEAT_PROVIDER_FEATURE_IMAGE_STATE,
    .backend=daemon?CLEAT_PROVIDER_BACKEND_DAEMON:CLEAT_PROVIDER_BACKEND_IN_PROCESS,
    .daemon_name=(const uint8_t *)"wh-image-baseline",.daemon_name_len=17};
  cleat_provider *p=cleat_provider_open(&pd);
  if(!p) return 3;
  cleat_session_desc sd={.cols=100,.rows=40,.cell_width_px=10,.cell_height_px=20,.vt_engine=CLEAT_PROVIDER_VT_GHOSTTY,
    .command=(const uint8_t *)argv[2],.command_len=strlen(argv[2]),.record=true};
  if(argc>3) { sd.id=(const uint8_t *)argv[3]; sd.id_len=strlen(argv[3]); }
  cleat_session *s=argc>3?cleat_session_attach(p,&sd):cleat_session_create(p,&sd);
  if(!s) return 4;
  Counts a={0},b={0},reconnected={0},history={0};
  int saw_streaming=0, saw_disconnect=0, saw_recovery=0;
  cleat_provider *p2=0; cleat_session *s2=0;
  for(int i=0;i<800;i++) {
    poll_view(s,&a);
    uint32_t connection=cleat_session_connection_state(s);
    if(connection==CLEAT_SESSION_STREAMING){ if(saw_disconnect)saw_recovery=1; saw_streaming=1; }
    if(saw_streaming && connection==CLEAT_SESSION_DISCONNECTED)saw_disconnect=1;
    if(i==200 && daemon) {
      cleat_str id={0}; cleat_session_id(s,&id); sd.id=id.ptr;sd.id_len=id.len;
      p2=cleat_provider_open(&pd); s2=cleat_session_attach(p2,&sd);
    }
    if(s2) poll_view(s2,i<400?&b:&reconnected);
    if(i==399 && s2) {
      report("second-viewer",s2,b);cleat_session_destroy(s2);cleat_provider_close(p2);
      p2=cleat_provider_open(&pd);s2=cleat_session_attach(p2,&sd);
    }
    usleep(10000);
  }
  report(argv[1],s,a);
  printf("transport disconnected=%d recovered=%d\n",saw_disconnect,saw_recovery);
  if(s2) report("reattached-viewer",s2,reconnected);
  cleat_viewport_command cmd={.kind=CLEAT_VIEWPORT_COMMAND_TOP};
  cleat_viewport_command_result result={0};
  printf("scroll accepted=%d ",cleat_session_scroll_viewport(s,&cmd,&result));printf("outcome=%u\n",result.outcome);
  for(int i=0;i<100;i++){poll_view(s,&history);usleep(10000);}
  report("history/top",s,history);
  int ok=a.updates>0 && (!daemon || (b.updates>0 && reconnected.updates>0));
  if(getenv("WH_EXPECT_RECONNECT")) ok=ok && saw_disconnect && saw_recovery;
  if(s2){cleat_session_destroy(s2);cleat_provider_close(p2);}
  cleat_session_destroy(s);cleat_provider_close(p);
  return ok?0:5;
}
