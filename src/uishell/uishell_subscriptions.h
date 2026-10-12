#ifndef UISHELL_SUBSCRIPTIONS_H
#define UISHELL_SUBSCRIPTIONS_H

#include "andamento.h"
#include "ingress/ingress.h"

//- The Dashboard's provider subscriptions (uishell_subscriptions.c).
typedef struct UIShell_Subscription UIShell_Subscription;
struct UIShell_Subscription
{
  UIShell_Subscription *next;
  Arena *arena;
  // Saved in the Dashboard: its subscription ID (a UUIDv7, made when it is
  // added), the provider its facts are stamped with; its kind; and how to
  // connect: the Flotilla daemon endpoint, empty for Flotilla's default.
  String8 id;
  String8 kind;
  String8 daemon;
  // While live: its own ingress endpoint, its connector, and whether its
  // provider is stale (the connector went down and hasn't published since).
  String8 endpoint;
  WheelhouseIngress *ingress;
  WheelhouseConnector *connector;
  B32 running;
  U64 starts;
  B32 stale;
  String8 error;
};

typedef struct UIShell_Subscriptions UIShell_Subscriptions;
struct UIShell_Subscriptions
{
  UIShell_Subscription *first, *last;
  U64 count;
  // Live once the local endpoint is up: each subscription's endpoint is
  // named after it, and its connector runs this Flotilla and logs here.
  // With --ingress_record, each endpoint records as the local one does, to
  // ingress-<subscription ID>.jsonl beside its ingress.jsonl.
  Arena *arena;
  B32 live;
  String8 local_endpoint, flotilla_bin, log_dir;
  B32 record;
  U64 record_bytes, record_files;
};

global UIShell_Subscriptions uishell_subscriptions;

internal void uishell_subscriptions_load(void);
internal void uishell_subscriptions_go_live(String8 local_endpoint, String8 flotilla_bin, String8 log_dir);
internal void uishell_subscriptions_close(void);
internal UIShell_Subscription *uishell_subscription_add(String8 kind, String8 daemon);
internal UIShell_Subscription *uishell_subscription_ensure(String8 kind, String8 daemon);
internal UIShell_Subscription *uishell_subscription_from_text(String8 text);
internal void uishell_subscription_remove(UIShell_Subscription *subscription);
internal void uishell_subscriptions_poll(void);
internal void uishell_subscription_set_stale(UIShell_Subscription *subscription, B32 stale);
internal void uishell_subscriptions_prepare_core(Andamento *core);
internal String8 uishell_subscription_label(Arena *arena, String8 provider);

#endif // UISHELL_SUBSCRIPTIONS_H
