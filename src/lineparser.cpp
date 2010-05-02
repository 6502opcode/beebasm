/*************************************************************************************************/
/**
	lineparser.cpp

	Represents a line of the source file
*/
/*************************************************************************************************/

#include "lineparser.h"
#include "asmexception.h"
#include "stringutils.h"
#include "symboltable.h"
#include "globaldata.h"
#include "sourcefile.h"


using namespace std;



/*************************************************************************************************/
/**
	LineParser::LineParser()

	Constructor for LineParser
*/
/*************************************************************************************************/
LineParser::LineParser( SourceFile* sourceFile, string line )
	:	m_sourceFile( sourceFile ),
		m_line( line ),
		m_column( 0 )
{
}



/*************************************************************************************************/
/**
	LineParser::~LineParser()

	Destructor for LineParser
*/
/*************************************************************************************************/
LineParser::~LineParser()
{
}



/*************************************************************************************************/
/**
	LineParser::ProcessLine()

	Process one line of the file
*/
/*************************************************************************************************/
void LineParser::Process()
{
	while ( AdvanceAndCheckEndOfLine() )	// keep going until we reach the end of the line
	{
//		cout << m_line << endl << string( m_column, ' ' ) << "^" << endl;

		int oldColumn = m_column;

		// Priority: check if it's symbol assignment and let it take priority over keywords
		// This means symbols can begin with reserved words, e.g. PLAyer, but in the case of
		// the line 'player = 1', the meaning is unambiguous, so we allow it as a symbol
		// assignment.

		bool bIsSymbolAssignment = false;

		if ( isalpha( m_line[ m_column ] ) || m_line[ m_column ] == '_' )
		{
			do
			{
				m_column++;

			} while ( ( isalpha( m_line[ m_column ] ) ||
						isdigit( m_line[ m_column ] ) ||
						m_line[ m_column ] == '_' ||
						m_line[ m_column ] == '%' ) &&
						m_line[ m_column - 1 ] != '%' );

			if ( AdvanceAndCheckEndOfStatement() )
			{
				if ( m_line[ m_column ] == '=' )
				{
					// if we have a valid symbol name, followed by an '=', it is definitely
					// a symbol assignment.
					bIsSymbolAssignment = true;
				}
			}
		}

		m_column = oldColumn;

		// first check tokens - they have priority over opcodes, so that they can have names
		// like INCLUDE (which would otherwise be interpreted as INC LUDE)

		if ( !bIsSymbolAssignment )
		{
			int token = GetTokenAndAdvanceColumn();

			if ( token != -1 )
			{
				HandleToken( token, oldColumn );
				continue;
			}
		}

		// Next we see if we should even be trying to execute anything.... maybe the if condition is false

		if ( !m_sourceFile->IsIfConditionTrue() )
		{
			m_column = oldColumn;
			SkipStatement();
			continue;
		}

		// No token match - check against opcodes

		if ( !bIsSymbolAssignment )
		{
			int token = GetInstructionAndAdvanceColumn();

			if ( token != -1 )
			{
				HandleAssembler( token );
				continue;
			}
		}

		// Check to see if it's symbol assignment

		if ( !isalpha( m_line[ m_column ] ) && m_line[ m_column ] != '_' )
		{
			throw AsmException_SyntaxError_UnrecognisedToken( m_line, m_column );
		}

		// Must be symbol assignment

		// Symbol starts with a valid character

		string symbolName = GetSymbolName() + m_sourceFile->GetSymbolNameSuffix();

		if ( !AdvanceAndCheckEndOfStatement() )
		{
			throw AsmException_SyntaxError_UnrecognisedToken( m_line, oldColumn );
		}

		if ( m_line[ m_column ] != '=' )
		{
			throw AsmException_SyntaxError_UnrecognisedToken( m_line, oldColumn );
		}

		m_column++;

		double value = EvaluateExpression();

		if ( GlobalData::Instance().IsFirstPass() )
		{
			// only add the symbol on the first pass

			if ( SymbolTable::Instance().IsSymbolDefined( symbolName ) )
			{
				throw AsmException_SyntaxError_LabelAlreadyDefined( m_line, oldColumn );
			}
			else
			{
				SymbolTable::Instance().AddSymbol( symbolName, value );
			}
		}

		if ( m_line[ m_column ] == ',' )
		{
			// Unexpected comma (remembering that an expression can validly end with a comma)
			throw AsmException_SyntaxError_UnexpectedComma( m_line, m_column );
		}

	}
}



/*************************************************************************************************/
/**
	LineParser::SkipStatement()

	Moves past the current statement to the next
*/
/*************************************************************************************************/
void LineParser::SkipStatement()
{
	bool bInQuotes = false;
	bool bInSingleQuotes = false;

	while ( m_column < m_line.length() && ( bInQuotes || bInSingleQuotes || MoveToNextAtom( ":;\\" ) ) )
	{
		if ( m_line[ m_column ] == '\"' && !bInSingleQuotes )
		{
			bInQuotes = !bInQuotes;
		}
		else if ( m_line[ m_column ] == '\'' )
		{
			if ( bInSingleQuotes )
			{
				bInSingleQuotes = false;
			}
			else if ( m_line[ m_column + 2 ] == '\'' && !bInQuotes )
			{
				bInSingleQuotes = true;
				m_column++;
			}
		}

		m_column++;
	}

	if ( m_line[ m_column ] == '\\' || m_line[ m_column ] == ';' )
	{
		m_column = m_line.length();
	}
	else if ( m_line[ m_column ] == ':' )
	{
		m_column++;
	}
}



/*************************************************************************************************/
/**
	LineParser::SkipExpression()

	Moves past the current expression to the next
*/
/*************************************************************************************************/
void LineParser::SkipExpression( int bracketCount, bool bAllowOneMismatchedCloseBracket )
{
	while ( AdvanceAndCheckEndOfSubStatement() )
	{
		if ( bAllowOneMismatchedCloseBracket )
		{
			if ( m_line[ m_column ] == '(' )
			{
				bracketCount++;
			}
			else if ( m_line[ m_column ] == ')' )
			{
				bracketCount--;

				if ( bracketCount < 0 )
				{
					break;
				}
			}
		}

		m_column++;
	}
}



/*************************************************************************************************/
/**
	LineParser::HandleToken()

	Calls the handler for the specified token

	@param		i				Index of the token to be handled
*/
/*************************************************************************************************/
void LineParser::HandleToken( int i, int oldColumn )
{
	assert( i >= 0 );

	// Do special case handling for IF/ELSE/ENDIF

	if ( m_gaTokenTable[ i ].m_handler == &LineParser::HandleIf )
	{
		m_sourceFile->AddIfLevel( m_line, m_column );
	}
	else if ( m_gaTokenTable[ i ].m_handler == &LineParser::HandleElse )
	{
		m_sourceFile->ToggleCurrentIfCondition( m_line, m_column );
	}
	else if ( m_gaTokenTable[ i ].m_handler == &LineParser::HandleEndif )
	{
		m_sourceFile->RemoveIfLevel( m_line, m_column );
	}

	if ( m_sourceFile->IsIfConditionTrue() )
	{
		( this->*m_gaTokenTable[ i ].m_handler )();
	}
	else
	{
		m_column = oldColumn;
		SkipStatement();
	}
}



/*************************************************************************************************/
/**
	LineParser::MoveToNextAtom()

	Moves the line pointer to the next significant atom to be parsed

	@return		bool		false if we reached a terminator
							true if the pointer is at a valid atom
*/
/*************************************************************************************************/
bool LineParser::MoveToNextAtom( const char* pTerminators )
{
	if ( !StringUtils::EatWhitespace( m_line, m_column ) )
	{
		return false;
	}

	if ( pTerminators != NULL )
	{
		size_t i = 0;

		while ( pTerminators[ i ] != 0 )
		{
			if ( m_line[ m_column ] == pTerminators[ i ] )
			{
				return false;
			}

			i++;
		}
	}

	return true;
}



/*************************************************************************************************/
/**
	LineParser::AdvanceAndCheckEndOfLine()

	Moves the line pointer to the next significant atom to be parsed, and checks whether the end
	of the line has been reached

	@return		bool		true if we are not yet at the end of the line
*/
/*************************************************************************************************/
bool LineParser::AdvanceAndCheckEndOfLine()
{
	return MoveToNextAtom();
}



/*************************************************************************************************/
/**
	LineParser::AdvanceAndCheckEndOfStatement()

	Moves the line pointer to the next significant atom to be parsed, and checks whether the end
	of the current statement has been reached.

	Valid statement terminators are the end of the line, a semicolon or backslash (comment), or
	a colon (statement separator)

	@return		bool		true if we are not yet at the end of the statement
*/
/*************************************************************************************************/
bool LineParser::AdvanceAndCheckEndOfStatement()
{
	return MoveToNextAtom( ";:\\" );
}



/*************************************************************************************************/
/**
	LineParser::AdvanceAndCheckEndOfSubStatement()

	Moves the line pointer to the next significant atom to be parsed, and checks whether the end
	of the current substatement has been reached.

	Valid substatement terminators are the end of the line, a semicolon or backslash (comment), a
	comma (substatement separator), or a colon (statement separator)

	@return		bool		true if we are not yet at the end of the substatement
*/
/*************************************************************************************************/
bool LineParser::AdvanceAndCheckEndOfSubStatement()
{
	return MoveToNextAtom( ";:\\," );
}



/*************************************************************************************************/
/**
	LineParser::GetSymbolName()

	This returns a valid symbol name, and advances the string pointer.
	It is assumed that we are already pointing to a valid symbol name.
*/
/*************************************************************************************************/
string LineParser::GetSymbolName()
{
	assert( isalpha( m_line[ m_column ] ) || m_line[ m_column ] == '_' );

	string symbolName;

	do
	{
		symbolName += m_line[ m_column++ ];

	} while ( ( isalpha( m_line[ m_column ] ) ||
				isdigit( m_line[ m_column ] ) ||
				m_line[ m_column ] == '_' ||
				m_line[ m_column ] == '%' ) &&
				m_line[ m_column - 1 ] != '%' );

	return symbolName;
}
