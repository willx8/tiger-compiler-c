#include "liveness.h"
#include "absyn.h"
#include "assem.h"
#include "flowgraph.h"
#include "frame.h"
#include "graph.h"
#include "symbol.h"
#include "table.h"
#include "temp.h"
#include "tree.h"
#include "util.h"

#include "move.h"
#include <stdio.h>

Live_moveList
Live_MoveList(G_node src, G_node dst, Live_moveList tail)
{
  Live_moveList lm = (Live_moveList)checked_malloc(sizeof(*lm));
  lm->src = src;
  lm->dst = dst;
  lm->tail = tail;
  lm->prev = NULL;
  if (tail) {
    tail->prev = lm;
  }
  return lm;
}

Temp_temp
Live_gtemp(G_node n)
{
  // your code here.
  return (Temp_temp)G_nodeInfo(n);
}

/*
 * caculate flow-graph liveness
 * expect in, out to be G_empty()ed.
 * no-return
 */
static void
createInOutTable(G_graph flow, G_table in, G_table out)
{
  assert(flow);
  G_nodeList nl_const = G_nodes(flow), nl, tl = NULL;

  // init.
  for (nl = nl_const; nl; nl = nl->tail) {
    G_enter(in, nl->head, NULL);
    G_enter(out, nl->head, NULL);
    if (!nl->tail) tl = nl;
  }

  // G_enter(out, tl->head, F_CallerSaves()); // add to out: caller save regs

  bool DONE = FALSE;
  while (!DONE) {
    nl = nl_const;
    DONE = TRUE;
    for (; nl; nl = nl->tail) {

      /* save in[n], out[n] */
      Temp_tempList intl = (Temp_tempList)G_look(in, nl->head);
      Temp_tempList outtl = (Temp_tempList)G_look(out, nl->head);
      Temp_tempList intl_d = Temp_copyList(intl);
      Temp_tempList outtl_d = Temp_copyList(outtl);
      assert(equals(intl, intl_d));
      assert(equals(outtl, outtl_d));

      /*
       * in[n] = use[n] U (out[n] - def[n])
       * equation 1
       */
      intl = unionn(FG_use(nl->head), except(outtl, FG_def(nl->head)));
      G_enter(in, nl->head, intl); // update in-table

      /*
       * out[n] = U in[s] {s, s->succ[n]}
       * equation 2
       */
      G_nodeList succ = G_succ(nl->head);
      for (; succ; succ = succ->tail) {
        outtl = unionn(outtl, (Temp_tempList)G_look(in, succ->head));
        // make 0 live range interfere
        outtl = unionn(outtl, FG_def(nl->head));
      }
      G_enter(out, nl->head, outtl); // update out-table

      // printInsNode(G_nodeInfo(nl->head)); // FG
      // printf("out:\n");
      // Temp_printList(outtl);
      // printf("\n");

      /*
       * repeat until in = in1, out = out1
       */
      if (!equals(intl_d, intl) || !equals(outtl, outtl_d)) {
        DONE = FALSE;
      }
      // printf(".");
    }
    //     printf("=====\n");
  }
  //  printf("last in:\n");
  //  Temp_printList(G_look(in, tl->head));
  //  printf("last out:\n");
  //  Temp_printList(G_look(out, tl->head));
  //  printf("\n");
}

static G_graph
initItfGraph(G_nodeList nl, TAB_table tempToNode)
{
  G_graph g = G_Graph();

  Temp_tempList temps = NULL;
  for (; nl; nl = nl->tail) {
    temps = unionn(temps, unionn(FG_def(nl->head), FG_use(nl->head)));
  }

  for (; temps; temps = temps->tail) {
    G_node n = G_Node(g, temps->head);
    TAB_enter(tempToNode, temps->head, n);
  }
  return g;
}

TAB_table tempMap;

static Live_moveList moves = NULL;

static G_graph
inteferenceGraph(G_nodeList nl, G_table liveMap)
{
  /* init a graph node-info save temp type
   * with create a all-regs list
   * with ctrate a temp -> node table
   */
  tempMap = TAB_empty(); // g only map node to temp. need a quick
                         // lookup for temp to node.
  G_graph g = initItfGraph(nl, tempMap);

  printf("inteferenceGraph:\n");
  G_show(stdout, G_nodes(g), Temp_print); // debug
  Temp_tempList liveouts;
  for (; nl; nl = nl->tail) {
    AS_instr i = (AS_instr)G_nodeInfo(nl->head);
    assert(i);
    Temp_tempList defs = FG_def(nl->head);

    if (i->kind == I_MOVE) {
      Temp_tempList srcs = FG_use(nl->head);
      assert(defs->tail == NULL); // our move instruction only have 1 def.
      assert(srcs->tail == NULL); // and from 1 reg.

      G_node dst = (G_node)TAB_look(tempMap, defs->head);
      G_node src = (G_node)TAB_look(tempMap, srcs->head);

      MOV_addlist(&moves, src, dst); // add to movelist

      liveouts = G_look(liveMap, nl->head);

      for (; liveouts; liveouts = liveouts->tail) {
        // look which node by map temp -> node
        G_node t = (G_node)TAB_look(tempMap, liveouts->head);

        if (dst == t) continue;

        // TODO:
        // We don't have to add next edge if we did coalescing.
        // currently we skip it so I comment it out.
        // when doing coalescing, uncomment next line.
        if (liveouts->head == srcs->head) continue;

        G_addEdge(dst, t);
      }
    }
    else { // i->kind == I_OPER

      printInsNode(G_nodeInfo(nl->head)); // FG

      ////printf("in:\n");
      ////Temp_printList(intl);
      // printf("out:\n");
      // Temp_printList(liveouts);
      // printf("\n");

      for (; defs; defs = defs->tail) {
        //   assert(i->kind == I_OPER || i->kind == I_LABEL);
        //   XXX:must place it here!!!
        //   otherwise second defs will get empty
        //   liveouts.
        liveouts = G_look(liveMap, nl->head);

        G_node dst = (G_node)TAB_look(tempMap, defs->head);
        for (; liveouts; liveouts = liveouts->tail) {
          G_node t = (G_node)TAB_look(tempMap, liveouts->head);

          if (dst == t) continue;
          G_addEdge(dst, t);
          //        printf("add edge:\n");
          //        Temp_print(liveouts->head);
          //        Temp_print(defs->head);
          //        printf("---\n");
        }
      }
    }
  }

  printf("\ninteferenceGraph:\n");
  G_show(stdout, G_nodes(g), Temp_print); // debug
  printf("end of inteferenceGraph\n");
  return g;
}

struct Live_graph
Live_liveness(G_graph flow)
{
  // your code here.
  struct Live_graph lg;

  printf("start. liveness\n");
  G_table in = G_empty();
  G_table out = G_empty();
  createInOutTable(flow, in, out); /* solution of data-flow */

  moves = MOV_set();
  G_graph g = inteferenceGraph(G_nodes(flow), out); // from out
  lg.graph = g;
  lg.moves = moves;
  return lg;
}
