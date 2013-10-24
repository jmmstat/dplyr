#define COMPILING_DPLYR
#include <dplyr.h>

using namespace Rcpp ;
using namespace dplyr ;

template <typename Index>
DataFrame subset( DataFrame df, const Index& indices, CharacterVector columns, CharacterVector classes){
    DataFrameVisitors visitors(df, columns) ;
    return visitors.subset(indices, classes) ;
}

template <typename Index>
DataFrame subset( DataFrame x, DataFrame y, const Index& indices_x, const Index& indices_y, CharacterVector by, CharacterVector classes ){
    CharacterVector x_columns = x.names() ;
    DataFrameVisitors visitors_x(x, x_columns) ;
    
    CharacterVector all_y_columns = y.names() ;
    CharacterVector y_columns = setdiff( all_y_columns, by ) ;
    DataFrameVisitors visitors_y(y, y_columns) ;
    
    int nrows = indices_x.size() ;
    int nv_x = visitors_x.size(), nv_y = visitors_y.size() ;
    List out(nv_x+nv_y);
    CharacterVector names(nv_x+nv_y) ;
    int k=0;
    for( ; k<nv_x; k++){
       out[k] = visitors_x.get(k)->subset(indices_x) ;
       names[k] = x_columns[k] ;
    }
    for( int i=0; i<nv_y; i++, k++){
       out[k] = visitors_y.get(i)->subset(indices_y) ; 
       names[k] = y_columns[i] ;
    }
    out.attr("class") = classes ;
    set_rownames(out, nrows) ;
    out.names() = names ;
    
    SEXP vars = x.attr( "vars" ) ;
    if( !Rf_isNull(vars) )
        out.attr( "vars" ) = vars ;
            
    return out.asSexp() ;
}

template <typename Container>
void push_back( Container& x, Container& y ){
    x.insert( x.end(), y.begin(), y.end() ) ;    
}
template <typename Container>
void push_back( Container& x, typename Container::value_type value, int n ){
    for( int i=0; i<n; i++)
        x.push_back( value ) ;    
}

// [[Rcpp::export]]
DataFrame semi_join_impl( DataFrame x, DataFrame y, CharacterVector by){
    typedef VisitorSetIndexMap<DataFrameJoinVisitors, std::vector<int> > Map ;
    DataFrameJoinVisitors visitors(x, y, by) ;
    Map map(visitors);  
    
    // train the map in terms of x
    train_push_back( map, x.nrows() ) ;
    
    int n_y = y.nrows() ;
    // this will collect indices from rows in x that match rows in y 
    std::vector<int> indices ;
    for( int i=0; i<n_y; i++){
        // find a row in x that matches row i from y
        Map::iterator it = map.find(-i-1) ;
        
        if( it != map.end() ){
            // collect the indices and remove them from the 
            // map so that they are only found once. 
            push_back( indices, it->second ) ;
        
            map.erase(it) ;
        
        }
    }
    
    return subset(x, indices, x.names(), x.attr("class") ) ;
}

// [[Rcpp::export]]
DataFrame anti_join_impl( DataFrame x, DataFrame y, CharacterVector by){
    typedef VisitorSetIndexMap<DataFrameJoinVisitors, std::vector<int> > Map ;
    DataFrameJoinVisitors visitors(x, y, by) ;
    Map map(visitors);  
    
    // train the map in terms of x
    train_push_back( map, x.nrows() ) ;
    
    int n_y = y.nrows() ;
    // remove the rows in x that match
    for( int i=0; i<n_y; i++){
        Map::iterator it = map.find(-i-1) ;
        if( it != map.end() )
            map.erase(it) ;
    }
    
    // collect what's left
    std::vector<int> indices ;
    for( Map::iterator it = map.begin() ; it != map.end(); ++it)
        push_back( indices, it->second ) ;
    
    return subset(x, indices, x.names(), x.attr( "class" ) ) ;
}

// [[Rcpp::export]]
DataFrame inner_join_impl( DataFrame x, DataFrame y, CharacterVector by){
    typedef VisitorSetIndexMap<DataFrameJoinVisitors, std::vector<int> > Map ;
    DataFrameJoinVisitors visitors(x, y, by) ;
    Map map(visitors);  
    
    // train the map in terms of x
    train_push_back( map, x.nrows() ) ;
    
    std::vector<int> indices_x ;
    std::vector<int> indices_y ;
    
    int n_y = y.nrows() ;
    for( int i=0; i<n_y; i++){
        // find indices for rows in x that match the row i in y
        Map::iterator it = map.find(-i-1) ;
        if( it != map.end() ){
            push_back( indices_x, it->second );
            push_back( indices_y, i, it->second.size() ) ;
        }
    }

    return subset( x, y, indices_x, indices_y, by, x.attr( "class") );
}


// [[Rcpp::export]]
DataFrame left_join_impl( DataFrame x, DataFrame y, CharacterVector by){
    typedef VisitorSetIndexMap<DataFrameJoinVisitors, std::vector<int> > Map ;
    DataFrameJoinVisitors visitors(y, x, by) ;
    Map map(visitors);  
    
    // train the map in terms of y
    train_push_back( map, y.nrows() ) ;
    
    std::vector<int> indices_x ;
    std::vector<int> indices_y ;
    
    int n_x = x.nrows() ;
    for( int i=0; i<n_x; i++){
        // find a row in y that matches row i in x
        Map::iterator it = map.find(-i-1) ;
        if( it != map.end() ){
            push_back( indices_y,    it->second ) ;
            push_back( indices_x, i, it->second.size() ) ;
        } else {
            indices_y.push_back(-1) ; // mark NA
            indices_x.push_back(i) ;
        }
    }

    return subset( x, y, indices_x, indices_y, by, x.attr( "class" ) ) ;
}

template <typename VisitorSet>
bool all_same_types(const VisitorSet& vx, const VisitorSet& vy){
    int n = vx.size() ;
    for( int i=0; i<n; i++){
        if( typeid(*vx.get(i)) != typeid(*vy.get(i)) )
            return false ;
    }
    return true ;
}

// [[Rcpp::export]]
dplyr::BoolResult compatible_data_frame( DataFrame x, DataFrame y, bool sort_variable_names = true ){
    int n = x.size() ;
    if( n != y.size() ) 
        return no_because( "not the same number of variables" ) ;
    
    CharacterVector names_x = clone<CharacterVector>(x.names()) ; 
    CharacterVector names_y = clone<CharacterVector>(y.names()) ; 
    if( sort_variable_names ){
        names_y.sort() ;
        names_x.sort() ;
    }
    
    for( int i=0; i<n; i++) 
        if( names_x[i] != names_y[i] )
            return no_because( "not the same variable names. ") ; 
    
    DataFrameVisitors v_x( x, names_x );
    DataFrameVisitors v_y( y, names_y );
    if( ! all_same_types(v_x, v_y ) )
        return no_because( "different types" ) ;
    
    return yes() ;
}

// [[Rcpp::export]]
dplyr::BoolResult equal_data_frame(DataFrame x, DataFrame y, bool sort_variable_names = true, bool sort_rows = true){
    BoolResult compat = compatible_data_frame(x, y, sort_variable_names);
    if( !compat ) return compat ;
    
    int nrows = x.nrows() ;
    if( nrows != y.nrows() )
        return no_because( "different row sizes" );
    
    if( sort_rows ){
        typedef VisitorSetIndexMap<DataFrameJoinVisitors, int > Map ;
        DataFrameJoinVisitors visitors(x, y, x.names() ) ;
        Map map(visitors);  
        
        for( int i=0; i<nrows; i++) map[i]++ ;
        for( int i=0; i<nrows; i++){
            Map::iterator it = map.find(-i-1) ;
            if( it == map.end() || it->second < 0 ) 
                return no_because( "different subset" ) ;
            else
                it->second-- ;
        }
    } else {
        DataFrameJoinVisitors visitors(x, y, x.names() ) ;
        for( int i=0; i<nrows; i++)
            if( !visitors.equal( i, -i-1) )
                return no_because( "different row" ) ;
    }
    return yes() ;
}

// [[Rcpp::export]]
dplyr::BoolResult all_equal_data_frame( List args, Environment env ){
    int n = args.size() ;
    DataFrame x0 = Rf_eval( args[0], env) ;
    for( int i=1; i<n; i++){
        BoolResult test = equal_data_frame( x0, Rf_eval( args[i], env ) ) ;
        if( !test ) return test ;
    }
    return yes() ;
}

// [[Rcpp::export]]
DataFrame union_data_frame( DataFrame x, DataFrame y){
    if( !compatible_data_frame(x,y) )
        stop( "not compatible" ); 
    
    typedef VisitorSetIndexSet<DataFrameJoinVisitors> Set ;
    DataFrameJoinVisitors visitors(x, y, x.names() ) ;
    Set set(visitors);  
    
    train_insert( set, x.nrows() ) ;
    train_insert_right( set, y.nrows() ) ;   
    
    return visitors.subset( set, x.attr("class") ) ;
}

// [[Rcpp::export]]
DataFrame intersect_data_frame( DataFrame x, DataFrame y){
    if( !compatible_data_frame(x,y) )
        stop( "not compatible" ); 
    
    typedef VisitorSetIndexSet<DataFrameJoinVisitors> Set ;
    DataFrameJoinVisitors visitors(x, y, x.names() ) ;
    Set set(visitors);  
    
    train_insert( set, x.nrows() ) ;
    
    std::vector<int> indices ;
    int n_y = y.nrows() ;
    for( int i=0; i<n_y; i++) {
        Set::iterator it = set.find( -i-1 ) ;
        if( it != set.end() ){
            indices.push_back(*it) ;
            set.erase(it) ;
        }
    }
    
    return visitors.subset( indices, x.attr("class") ) ;
}

// [[Rcpp::export]]
DataFrame setdiff_data_frame( DataFrame x, DataFrame y){
    if( !compatible_data_frame(x,y) )
        stop( "not compatible" ); 
    
    typedef VisitorSetIndexSet<DataFrameJoinVisitors> Set ;
    DataFrameJoinVisitors visitors(y, x, y.names() ) ;
    Set set(visitors);  
    
    train_insert( set, y.nrows() ) ;
    
    std::vector<int> indices ;
    
    int n_x = x.nrows() ;
    for( int i=0; i<n_x; i++) {
        if( !set.count(-i-1) ){
            set.insert(-i-1) ;
            indices.push_back(-i-1) ;
        }
    }
    
    return visitors.subset( indices, x.attr("class") ) ;
}

// [[Rcpp::export]]
IntegerVector match_data_frame( DataFrame x, DataFrame y){
    if( !compatible_data_frame(x,y) )
        stop( "not compatible" ); 
    
    typedef VisitorSetIndexSet<DataFrameJoinVisitors> Set ;
    DataFrameJoinVisitors visitors(y, x, x.names() ) ;
    Set set(visitors);  
    
    train_insert( set, y.nrows() ) ;
    
    int n_x = x.nrows() ;
    IntegerVector res = no_init( n_x );
    for( int i=0; i<n_x; i++) {
        Set::iterator it = set.find( -i-1 );
        res[i] = ( it == set.end() ) ? NA_INTEGER : (*it+1) ;
    }
    
    return res ;
}

// [[Rcpp::export]]
DataFrame build_index_cpp( DataFrame data ){
    CharacterVector vars = Rf_getAttrib( data.attr( "vars" ), R_NamesSymbol ) ;
    
    DataFrameVisitors visitors(data, vars) ;
    ChunkIndexMap map( visitors ) ;
    train_push_back( map, data.nrows() ) ;
    
    data.attr( "index" )  = get_all_second(map) ;
    data.attr( "labels" ) = visitors.subset(map, "data.frame" ) ;
    return data ;
}

SEXP and_calls( List args ){
    int ncalls = args.size() ;
    if( !ncalls ) return Rf_ScalarLogical(TRUE) ;
    
    Rcpp::Armor<SEXP> res( args[0] ) ;
    SEXP and_symbol = Rf_install( "&" ) ;
    for( int i=1; i<ncalls; i++)
        res = Rcpp_lang3( and_symbol, res, args[i] ) ;
    return res ;
}

DataFrame subset( DataFrame data, LogicalVector test, CharacterVector select, CharacterVector classes ){
    DataFrameVisitors visitors( data, select ) ;
    return visitors.subset(test, classes ) ;
}

DataFrame filter_grouped( const GroupedDataFrame& gdf, List args, Environment env){
    // a, b, c ->  a & b & c
    Language call = and_calls( args ) ;
    
    DataFrame data = gdf.data() ;
    int nrows = data.nrows() ;
    LogicalVector test = no_init(nrows);
    
    LogicalVector g_test ;
    CallProxy call_proxy( call, data ) ;
    int ngroups = gdf.ngroups() ;
    for( int i=0; i<ngroups; i++){
        Index_0_based indices = gdf.group(i) ;
        g_test  = call_proxy.get( indices );
        
        int chunk_size = indices.size() ;
        for( int j=0; j<chunk_size; j++){
            test[ indices[j] ] = g_test[j] ;  
        }
    }
    
    DataFrame res = subset( data, test, data.names(), classes_grouped() ) ;
    res.attr( "vars")   = gdf.attr("vars") ;
            
    return res ;
}

SEXP filter_not_grouped( DataFrame df, List args, Environment env){
    // a, b, c ->  a & b & c
    Language call = and_calls( args ) ;
    
    // replace the symbols that are in the data frame by vectors from the data frame
    // and evaluate the expression
    LogicalVector test = CallProxy( call, df).get( Everything() ) ;
    
    DataFrame res = subset( df, test, df.names(), classes_not_grouped() ) ;
    return res ;
}

// [[Rcpp::export]]
SEXP filter_impl( DataFrame df, List args, Environment env){
    if( is<GroupedDataFrame>( df ) ){
        return filter_grouped( GroupedDataFrame(df), args, env);    
    } else {
        return filter_not_grouped( df, args, env) ;   
    }
}

SEXP structure_mutate( CallProxy& call_proxy, const DataFrame& df, const CharacterVector& results_names, CharacterVector classes){
    int n = call_proxy.nsubsets() ;
    
    List out(n) ;
    CharacterVector names(n) ;
    
    CharacterVector input_names = df.names() ;
    int ncolumns = df.size() ;
    int i=0 ;
    for( ; i<ncolumns; i++){
        out[i] = call_proxy.get_variable(input_names[i]) ;
        SET_NAMED( out[i], 2 );
        names[i] = input_names[i] ;
    }
    for( int k=0; i<n; k++ ){
        String name = results_names[k] ;
        
        if( ! any( input_names.begin(), input_names.end(), name.get_sexp() ) ){
            SEXP x   = call_proxy.get_variable( name ) ; 
            out[i]   = x ;
            SET_NAMED( out[i], 2 );
            names[i] = name ;
            i++ ;
        }
    }
    
    
    out.attr("class") = classes ;
    set_rownames( out, df.nrows() ) ;
    out.names() = names;
    
    return out ;    
}

SEXP mutate_grouped(GroupedDataFrame gdf, List args, Environment env){
    DataFrame df = gdf.data() ;
    
    int nexpr = args.size() ;
    CharacterVector results_names = args.names() ;
    
    CallProxy call_proxy(df) ;
    Shelter<SEXP> __ ;
    
    for( int i=0; i<nexpr; i++){
        call_proxy.set_call( args[i] );
        Gatherer* gather = gatherer( call_proxy, gdf );
        SEXP res = __( gather->collect() ) ;
        delete gather ;
        call_proxy.input( results_names[i], res ) ;
    }
    
    DataFrame res = structure_mutate( call_proxy, df, results_names, classes_grouped() ) ;
    res.attr( "vars")    = gdf.attr("vars") ;
    res.attr( "labels" ) = gdf.attr("labels" );
    res.attr( "index")   = gdf.attr("index") ;
    
    return res ;
}

SEXP mutate_not_grouped(DataFrame df, List args, Environment env){
    Shelter<SEXP> __ ;
    
    int nexpr = args.size() ;
    CharacterVector results_names = args.names() ;
    
    CallProxy call_proxy(df) ;
    for( int i=0; i<nexpr; i++){
        call_proxy.set_call( args[i] );
        
        // we need to protect the SEXP, that's what the Shelter does
        SEXP res = __( call_proxy.get( Everything() ) ) ;
        call_proxy.input( results_names[i], res ) ;
        
    }
    
    DataFrame res = structure_mutate(call_proxy, df, results_names, classes_not_grouped() ) ;
    
    return res ;
}


// [[Rcpp::export]]
SEXP mutate_impl( DataFrame df, List args, Environment env){
    if( is<GroupedDataFrame>( df ) ){
        return mutate_grouped( GroupedDataFrame(df), args, env);    
    } else {
        return mutate_not_grouped( df, args, env) ;   
    }
}

// [[Rcpp::export]] 
IntegerVector order_impl( List args, Environment env ){
    int nargs = args.size() ;  
    SEXP tmp ;
    List variables(nargs) ; 
    LogicalVector ascending(nargs) ;
    for(int i=0; i<nargs; i++){
        tmp = args[i] ;
        if( TYPEOF(tmp) == LANGSXP && CAR(tmp) == Rf_install("desc") ){
            variables[i] = Rf_eval( CAR(CDR(tmp) ), env ) ;
            ascending[i] = false ;
        } else{
            variables[i] = Rf_eval( tmp, env );
            ascending[i] = true ;
        }
    }
    OrderVisitors o(variables,ascending, nargs) ;
	IntegerVector res = o.apply() ;
	res = res + 1 ;
	return res ;
}

// [[Rcpp::export]] 
DataFrame arrange_impl( DataFrame data, List args, Environment env ){
    int nargs = args.size() ;  
    SEXP tmp ;
    List variables(nargs) ; 
    LogicalVector ascending(nargs) ;
    for(int i=0; i<nargs; i++){
        tmp = args[i] ;
        if( TYPEOF(tmp) == LANGSXP && CAR(tmp) == Rf_install("desc") ){
            variables[i] = Rf_eval( CAR(CDR(tmp) ), env ) ;
            ascending[i] = false ;
        } else{
            variables[i] = Rf_eval( tmp, env );
            ascending[i] = true ;
        }
    }
    OrderVisitors o(variables,ascending, nargs) ;
	IntegerVector index = o.apply() ;
	
	DataFrameVisitors visitors( data, data.names() ) ;
	DataFrame res = visitors.subset(index, data.attr("class") ) ;
	return res;
}

// [[Rcpp::export]] 
DataFrame sort_impl( DataFrame data ){
    OrderVisitors o(data) ;
    IntegerVector index = o.apply() ;
    
    DataFrameVisitors visitors( data, data.names() ) ;
    DataFrame res = visitors.subset(index, "data.frame" ) ;
    return res;
}


namespace dplyr {
    
    OrderVisitors::OrderVisitors( List args, Rcpp::LogicalVector ascending, int n_ ) : 
        visitors(n_), n(n_), nrows(0){
        nrows = Rf_length( args[0] );
        for( int i=0; i<n; i++)
            visitors[i]  = order_visitor( args[i], ascending[i] );
    } 
    
    OrderVisitors::OrderVisitors( DataFrame data ) : 
        visitors(data.size()), n(data.size()), nrows( data.nrows() )
    {
        for( int i=0; i<n; i++)
            visitors[i]  = order_visitor( data[i], true );
    } 
    
    OrderVisitors::OrderVisitors( DataFrame data, CharacterVector names ) : 
        visitors(data.size()), n(data.size()), nrows( data.nrows() )
    {
        for( int i=0; i<n; i++){
            String name = names[i] ;
            visitors[i]  = order_visitor( data[name], true );
        }
    } 
    
    OrderVisitors_Compare::OrderVisitors_Compare( const OrderVisitors& obj_ ) : 
        obj(obj_), n(obj.n){}
    
    IntegerVector OrderVisitors::apply() const {
        IntegerVector x = seq(0, nrows -1 ) ;
        std::sort( x.begin(), x.end(), OrderVisitors_Compare(*this) ) ;
        return x ;
    }
    
    bool OrderVisitors_Compare::operator()(int i, int j) const {
        if( i == j ) return false ;
        for( int k=0; k<n; k++)
            if( ! obj.visitors[k]->equal(i,j) )
                return obj.visitors[k]->before(i, j ) ; 
        return i < j ;
        
    }

    OrderVisitors::~OrderVisitors(){
        delete_all( visitors ) ;
    }
    
    
}