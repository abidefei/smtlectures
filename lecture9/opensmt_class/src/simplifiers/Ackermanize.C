/*********************************************************************
Author: Roberto Bruttomesso <roberto.bruttomesso@gmail.com>

OpenSMT -- Copyright (C) 2009, Roberto Bruttomesso

OpenSMT is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

OpenSMT is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenSMT. If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#include "Ackermanize.h"

Enode *
Ackermanize::doit( Enode * formula )
{
  assert( config.logic == QF_UFIDL
       || config.logic == QF_UFLRA );

  map< Enode *, vector< Enode * > > uf_to_ack_vars;
  map< Enode *, vector< Enode * > > uf_to_ack_args;

  Enode * purified = retrieveAckVarsAndArguments( formula, uf_to_ack_vars, uf_to_ack_args );
  Enode * result   = generateAckermannExpansion ( purified, uf_to_ack_vars, uf_to_ack_args ); 

  return result;
}

Enode *
Ackermanize::retrieveAckVarsAndArguments( Enode * formula
                                        , map< Enode *, vector< Enode * > > & uf_to_ack_vars
                                        , map< Enode *, vector< Enode * > > & uf_to_ack_args )
{
  assert( formula );
  vector< Enode * > unprocessed_enodes;
  egraph.initDupMap1( );

  unprocessed_enodes.push_back( formula );

  char def_name[ 256 ];
  //
  // Visit the DAG of the formula from the leaves to the root
  //
  while( !unprocessed_enodes.empty( ) )
  {
    Enode * enode = unprocessed_enodes.back( );
    // 
    // Skip if the node has already been processed before
    //
    if ( egraph.valDupMap1( enode ) != NULL )
    {
      unprocessed_enodes.pop_back( );
      continue;
    }

    bool unprocessed_children = false;
    Enode * arg_list;
    for ( arg_list = enode->getCdr( ) ; 
	  arg_list != egraph.enil ; 
	  arg_list = arg_list->getCdr( ) )
    {
      Enode * arg = arg_list->getCar( );

      assert( arg->isTerm( ) );
      //
      // Push only if it is unprocessed
      //
      if ( egraph.valDupMap1( arg ) == NULL )
      {
	unprocessed_enodes.push_back( arg );
	unprocessed_children = true;
      }
    }
    //
    // SKip if unprocessed_children
    //
    if ( unprocessed_children )
      continue;

    unprocessed_enodes.pop_back( );                      
    Enode * result = NULL;
    //
    // At this point, every child has been processed
    //
    if ( enode->isUf( ) || enode->isUp( ) )
    {
      sprintf( def_name, "%s_app_%d", (enode->getCar( )->getName( )).c_str( )
	                            , enode->getId( ) );
      // Check buffer overflow in name
      assert( (enode->getCar( )->getName( )).length( ) <= 240 );
      Snode * s = enode->getRetSort( );
      egraph.newSymbol( def_name, NULL, s );  
      result = egraph.mkVar( def_name );
      // Store variable in map for f
      uf_to_ack_vars[ enode->getCar( ) ].push_back( result );
      // Retrieve arguments
      for ( Enode * arg_list = enode->getCdr( ) 
	  ; !arg_list->isEnil( ) 
	  ; arg_list = arg_list->getCdr( ) )
      {
	Enode * new_arg = egraph.valDupMap1( arg_list->getCar( ) );
	uf_to_ack_args[ enode->getCar( ) ].push_back( new_arg );
      }
      assert( uf_to_ack_args.find( enode->getCar( ) ) != uf_to_ack_args.end( ) );
    }
    else
    {
      result = egraph.copyEnodeEtypeTermWithCache( enode );
    }

    assert( result );
    assert( egraph.valDupMap1( enode ) == NULL );
    egraph.storeDupMap1( enode, result );
  }

  Enode * new_formula = egraph.valDupMap1( formula );
  assert( new_formula );
  egraph.doneDupMap1( );

  return new_formula;
}

Enode * 
Ackermanize::generateAckermannExpansion( Enode * formula
                                       , map< Enode *, vector< Enode * > > & uf_to_ack_vars
                                       , map< Enode *, vector< Enode * > > & uf_to_ack_args )
{
  list< Enode * > new_axioms_list;
  //
  // For each functional symbol
  //
  for ( map< Enode *, vector< Enode * > >::iterator it = uf_to_ack_vars.begin( )
      ; it != uf_to_ack_vars.end( )
      ; it ++ )
  {
    assert( uf_to_ack_args.find( it->first ) != uf_to_ack_args.end( ) );
    vector< Enode * > & args = uf_to_ack_args[ it->first ];

    Enode * s = it->first;
    const int arity = s->getArity( );
    vector< Enode * > & vars = it->second;
    //
    // For each ack variable
    //
    for ( unsigned i = 0 ; i < vars.size( ) - 1 ; i ++ )
    {
      const int index_i = arity * i;
      for ( unsigned j = i + 1 ; j < vars.size( ) ; j ++ )
      {
	const int index_j = arity * j;
	//
	// Construct (ai_1 = aj_1 /\ ai_2 = aj_2 /\ ... /\ ai_n = aj_n) --> var_i = var_j
	// equiv to  (ai_1 != aj_1 \/ ai_2 != aj_2 \/ ... \/ ai_n != aj_n \/ var_i = var_j)
	//
	// equiv to  (ai_1 < aj_1 \/ ai_1 > aj_1 \/ ... \/ var_i <= var_j)
	// equiv to  (ai_1 < aj_1 \/ ai_1 > aj_1 \/ ... \/ var_i => var_j)
	//
	list< Enode * > axiom;

	for ( int k = 0 ; k < arity ; k ++ )
	{
	  Enode * ai_k = args[ index_i + k ];
	  Enode * aj_k = args[ index_j + k ];
	  // Generate ai_1 < aj_1
	  Enode * lt = egraph.mkLt( egraph.cons( ai_k, egraph.cons( aj_k ) ) );
	  Enode * gt = egraph.mkGt( egraph.cons( ai_k, egraph.cons( aj_k ) ) );
	  axiom.push_back( lt );
	  axiom.push_back( gt );
	}
	//
	// Construct 
	// var_i <= var_j and var_i >= var_j
	// or for the boolean case
	// var_i -> var_j and var_i <- var_j
	//
	Enode * le = NULL; 
	Enode * ge = NULL; 
	const bool is_bool = vars[ i ]->hasSortBool( );
	if ( is_bool )
	{
	  le = egraph.mkOr( egraph.cons( egraph.mkNot( egraph.cons( vars[ i ] ) )
			  , egraph.cons( vars[ j ] ) ) );
	  ge = egraph.mkOr( egraph.cons( egraph.mkNot( egraph.cons( vars[ j ] ) )
		          , egraph.cons( vars[ i ] ) ) );
	}
	else
	{
	  le = egraph.mkLeq( egraph.cons( vars[ i ]
	                   , egraph.cons( vars[ j ] ) ) );
	  ge = egraph.mkGeq( egraph.cons( vars[ i ]
	                   , egraph.cons( vars[ j ] ) ) );
	}
	axiom.push_back( le );
	Enode * axiom_1 = egraph.mkOr( egraph.cons( axiom ) );
	axiom.pop_back( );
	axiom.push_back( ge );
	Enode * axiom_2 = egraph.mkOr( egraph.cons( axiom ) );
	new_axioms_list.push_back( axiom_1 );
	new_axioms_list.push_back( axiom_2 );
      }
    }
  }

  new_axioms_list.push_back( formula );
  Enode * result = egraph.mkAnd( egraph.cons( new_axioms_list ) );
  return result;
}
