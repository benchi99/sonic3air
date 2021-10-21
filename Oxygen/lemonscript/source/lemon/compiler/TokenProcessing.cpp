/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2021 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "lemon/pch.h"
#include "lemon/compiler/TokenProcessing.h"
#include "lemon/compiler/TokenTypes.h"
#include "lemon/compiler/Utility.h"
#include "lemon/program/GlobalsLookup.h"


namespace lemon
{

	namespace
	{
		static const uint8 operatorPriorityLookup[] =
		{
			15,	 // ASSIGN
			15,	 // ASSIGN_PLUS
			15,	 // ASSIGN_MINUS
			15,	 // ASSIGN_MULTIPLY
			15,	 // ASSIGN_DIVIDE
			15,	 // ASSIGN_MODULO
			15,	 // ASSIGN_SHIFT_LEFT
			15,	 // ASSIGN_SHIFT_RIGHT
			15,	 // ASSIGN_AND
			15,	 // ASSIGN_OR
			15,	 // ASSIGN_XOR
			6,	 // BINARY_PLUS
			6,	 // BINARY_MINUS
			5,	 // BINARY_MULTIPLY
			5,	 // BINARY_DIVIDE
			5,	 // BINARY_MODULO
			7,	 // BINARY_SHIFT_LEFT
			7,	 // BINARY_SHIFT_RIGHT
			10,	 // BINARY_AND
			12,	 // BINARY_OR
			11,	 // BINARY_XOR
			13,	 // LOGICAL_AND
			14,	 // LOGICAL_OR
			3,   // UNARY_NOT
			3,   // UNARY_BITNOT
			3,	 // UNARY_DECREMENT (actually 2 for post-, 3 for pre-decrement)
			3,	 // UNARY_INCREMENT (same here)
			9,	 // COMPARE_EQUAL
			9,	 // COMPARE_NOT_EQUAL
			8,	 // COMPARE_LESS
			8,	 // COMPARE_LESS_OR_EQUAL
			8,	 // COMPARE_GREATER
			8,	 // COMPARE_GREATER_OR_EQUAL
			15,	 // QUESTIONMARK
			15,	 // COLON
			18,	 // SEMICOLON_SEPARATOR (only in 'for' statements, otherwise ignored)
			17,	 // COMMA_SEPARATOR (should be evaluated separatedly, after all others)
			2,	 // PARENTHESIS_LEFT
			2,	 // PARENTHESIS_RIGHT
			2,	 // BRACKET_LEFT
			2	 // BRACKET_RIGHT
		};
		static_assert(sizeof(operatorPriorityLookup) == (size_t)Operator::_NUM_OPERATORS, "Update operator priority lookup");

		static const bool operatorAssociativityLookup[] =
		{
			// "false" = left to right
			// "true" = right to left
			false,		// Priority 0 (unused)
			false,		// Priority 1 (reserved for :: operator)
			false,		// Priority 2 (parentheses)
			true,		// Priority 3 (unary operators)
			false,		// Priority 4 (reserved for element access)
			false,		// Priority 5 (multiplication, division)
			false,		// Priority 6 (addition, subtraction)
			false,		// Priority 7 (shifts)
			false,		// Priority 8 (comparisons)
			false,		// Priority 9 (comparisons)
			false,		// Priority 10 (bitwise AND)
			false,		// Priority 11 (bitwise XOR)
			false,		// Priority 12 (bitwise OR)
			false,		// Priority 13 (logical AND)
			false,		// Priority 14 (logical OR)
			true,		// Priority 15 (assignments and trinary operator)
			true,		// Priority 16 (reserved for throw)
			false		// Priority 17 (comma separator)
		};

		uint8 getImplicitCastPriority(const DataTypeDefinition* original, const DataTypeDefinition* target)
		{
			const uint8 CANNOT_CAST = 0xff;
			if (original == target)
			{
				// No cast required at all
				return 0;
			}

			if (original->mClass == DataTypeDefinition::Class::INTEGER && target->mClass == DataTypeDefinition::Class::INTEGER)
			{
				const IntegerDataType& originalInt = original->as<IntegerDataType>();
				const IntegerDataType& targetInt = target->as<IntegerDataType>();

				// Is one type undefined?
				if (originalInt.mSemantics == IntegerDataType::Semantics::CONSTANT)
				{
					// Const may get cast to everything
					return 1;
				}
				if (targetInt.mSemantics == IntegerDataType::Semantics::CONSTANT)
				{
					// Can this happen at all?
					return 1;
				}

				if (originalInt.mBytes == targetInt.mBytes)
				{
					return (originalInt.mIsSigned && !targetInt.mIsSigned) ? 0x02 : 0x01;
				}
				
				const uint8 a = (uint8)DataTypeHelper::getBaseType(original);
				const uint8 b = (uint8)DataTypeHelper::getBaseType(target);
				const uint8 sizeA = a & 0x07;
				const uint8 sizeB = b & 0x07;
				if (originalInt.mBytes < targetInt.mBytes)
				{
					// Up cast
					return ((originalInt.mIsSigned && !targetInt.mIsSigned) ? 0x20 : 0x10) + (sizeB - sizeA);
				}
				else
				{
					// Down cast
					return ((originalInt.mIsSigned && !targetInt.mIsSigned) ? 0x40 : 0x30) + (sizeB - sizeA);
				}
			}
			else
			{
				// No cast between non-integers
				return CANNOT_CAST;
			}
		}

		struct BinaryOperatorSignature
		{
			const DataTypeDefinition* mLeft;
			const DataTypeDefinition* mRight;
			const DataTypeDefinition* mResult;
			inline BinaryOperatorSignature(const DataTypeDefinition* left, const DataTypeDefinition* right, const DataTypeDefinition* result) : mLeft(left), mRight(right), mResult(result) {}
		};

		uint16 getPriorityOfSignature(const BinaryOperatorSignature& signature, const DataTypeDefinition* left, const DataTypeDefinition* right)
		{
			const uint8 prioLeft = getImplicitCastPriority(left, signature.mLeft);
			const uint8 prioRight = getImplicitCastPriority(right, signature.mRight);
			if (prioLeft < prioRight)
			{
				return ((uint16)prioRight << 8) + (uint16)prioLeft;
			}
			else
			{
				return ((uint16)prioLeft << 8) + (uint16)prioRight;
			}
		}

		uint32 getPriorityOfSignature(const std::vector<const DataTypeDefinition*>& original, const Function::ParameterList& target)
		{
			if (original.size() != target.size())
				return 0xffffffff;

			const size_t size = original.size();
			static std::vector<uint8> priorities;	// Not multi-threading safe
			priorities.resize(size);

			for (size_t i = 0; i < size; ++i)
			{
				priorities[i] = getImplicitCastPriority(original[i], target[i].mType);
			}

			// Highest priority should be first
			std::sort(priorities.begin(), priorities.end(), std::greater<uint8>());

			uint32 result = 0;
			for (size_t i = 0; i < std::min<size_t>(size, 4); ++i)
			{
				result |= (priorities[i] << (24 - i * 8));
			}
			return result;
		}

		enum class OperatorType
		{
			ASSIGNMENT,
			SYMMETRIC,
			COMPARISON,
			TRINARY,
			UNKNOWN
		};

		OperatorType getOperatorType(Operator op)
		{
			switch (op)
			{
				case Operator::ASSIGN:
				case Operator::ASSIGN_PLUS:
				case Operator::ASSIGN_MINUS:
				case Operator::ASSIGN_MULTIPLY:
				case Operator::ASSIGN_DIVIDE:
				case Operator::ASSIGN_MODULO:
				case Operator::ASSIGN_SHIFT_LEFT:	// TODO: Special handling required
				case Operator::ASSIGN_SHIFT_RIGHT:	// TODO: Special handling required
				case Operator::ASSIGN_AND:
				case Operator::ASSIGN_OR:
				case Operator::ASSIGN_XOR:
				{
					return OperatorType::ASSIGNMENT;
				}

				case Operator::BINARY_PLUS:
				case Operator::BINARY_MINUS:
				case Operator::BINARY_MULTIPLY:
				case Operator::BINARY_DIVIDE:
				case Operator::BINARY_MODULO:
				case Operator::BINARY_SHIFT_LEFT:	// TODO: Special handling required
				case Operator::BINARY_SHIFT_RIGHT:	// TODO: Special handling required
				case Operator::BINARY_AND:
				case Operator::BINARY_OR:
				case Operator::BINARY_XOR:
				case Operator::LOGICAL_AND:
				case Operator::LOGICAL_OR:
				case Operator::COLON:
				{
					return OperatorType::SYMMETRIC;
				}

				case Operator::COMPARE_EQUAL:
				case Operator::COMPARE_NOT_EQUAL:
				case Operator::COMPARE_LESS:
				case Operator::COMPARE_LESS_OR_EQUAL:
				case Operator::COMPARE_GREATER:
				case Operator::COMPARE_GREATER_OR_EQUAL:
				{
					return OperatorType::COMPARISON;
				}

				case Operator::QUESTIONMARK:
				{
					return OperatorType::TRINARY;
				}

				default:
				{
					return OperatorType::UNKNOWN;
				}
			}
		}

		bool getBestSignature(Operator op, const DataTypeDefinition* left, const DataTypeDefinition* right, const BinaryOperatorSignature** outSignature)
		{
			static const std::vector<BinaryOperatorSignature> signaturesSymmetric =
			{
				// TODO: This is oversimplified, there are cases like multiply and left-shift (and probably also add / subtract) that require different handling
				BinaryOperatorSignature(&PredefinedDataTypes::INT_64,  &PredefinedDataTypes::INT_64,  &PredefinedDataTypes::INT_64),
				BinaryOperatorSignature(&PredefinedDataTypes::UINT_64, &PredefinedDataTypes::UINT_64, &PredefinedDataTypes::UINT_64),
				BinaryOperatorSignature(&PredefinedDataTypes::INT_32,  &PredefinedDataTypes::INT_32,  &PredefinedDataTypes::INT_32),
				BinaryOperatorSignature(&PredefinedDataTypes::UINT_32, &PredefinedDataTypes::UINT_32, &PredefinedDataTypes::UINT_32),
				BinaryOperatorSignature(&PredefinedDataTypes::INT_16,  &PredefinedDataTypes::INT_16,  &PredefinedDataTypes::INT_16),
				BinaryOperatorSignature(&PredefinedDataTypes::UINT_16, &PredefinedDataTypes::UINT_16, &PredefinedDataTypes::UINT_16),
				BinaryOperatorSignature(&PredefinedDataTypes::INT_8,   &PredefinedDataTypes::INT_8,   &PredefinedDataTypes::INT_8),
				BinaryOperatorSignature(&PredefinedDataTypes::UINT_8,  &PredefinedDataTypes::UINT_8,  &PredefinedDataTypes::UINT_8)
			};
			static const std::vector<BinaryOperatorSignature> signaturesComparison =
			{
				// Result types are always bool
				BinaryOperatorSignature(&PredefinedDataTypes::INT_64,  &PredefinedDataTypes::INT_64,  &PredefinedDataTypes::BOOL),
				BinaryOperatorSignature(&PredefinedDataTypes::UINT_64, &PredefinedDataTypes::UINT_64, &PredefinedDataTypes::BOOL),
				BinaryOperatorSignature(&PredefinedDataTypes::INT_32,  &PredefinedDataTypes::INT_32,  &PredefinedDataTypes::BOOL),
				BinaryOperatorSignature(&PredefinedDataTypes::UINT_32, &PredefinedDataTypes::UINT_32, &PredefinedDataTypes::BOOL),
				BinaryOperatorSignature(&PredefinedDataTypes::INT_16,  &PredefinedDataTypes::INT_16,  &PredefinedDataTypes::BOOL),
				BinaryOperatorSignature(&PredefinedDataTypes::UINT_16, &PredefinedDataTypes::UINT_16, &PredefinedDataTypes::BOOL),
				BinaryOperatorSignature(&PredefinedDataTypes::INT_8,   &PredefinedDataTypes::INT_8,   &PredefinedDataTypes::BOOL),
				BinaryOperatorSignature(&PredefinedDataTypes::UINT_8,  &PredefinedDataTypes::UINT_8,  &PredefinedDataTypes::BOOL)
			};
			static const std::vector<BinaryOperatorSignature> signaturesTrinary =
			{
				BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::INT_64,  &PredefinedDataTypes::INT_64),
				BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::UINT_64, &PredefinedDataTypes::UINT_64),
				BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::INT_32,  &PredefinedDataTypes::INT_32),
				BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::UINT_32, &PredefinedDataTypes::UINT_32),
				BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::INT_16,  &PredefinedDataTypes::INT_16),
				BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::UINT_16, &PredefinedDataTypes::UINT_16),
				BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::INT_8,   &PredefinedDataTypes::INT_8),
				BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::UINT_8,  &PredefinedDataTypes::UINT_8)
			};

			const std::vector<BinaryOperatorSignature>* signatures = nullptr;
			bool exactMatchLeftRequired = false;

			switch (getOperatorType(op))
			{
				case OperatorType::ASSIGNMENT:
				{
					signatures = &signaturesSymmetric;
					exactMatchLeftRequired = true;
					break;
				}

				case OperatorType::SYMMETRIC:
				{
					signatures = &signaturesSymmetric;
					break;
				}

				case OperatorType::COMPARISON:
				{
					signatures = &signaturesComparison;
					break;
				}

				case OperatorType::TRINARY:
				{
					signatures = &signaturesTrinary;
					break;
				}

				default:
				{
					// This should never happen
					CHECK_ERROR_NOLINE(false, "Unknown operator type");
				}
			}

			uint16 bestPriority = 0xff00;
			for (const BinaryOperatorSignature& signature : *signatures)
			{
				if (exactMatchLeftRequired)
				{
					if (signature.mLeft != left)
						continue;
				}

				const uint16 priority = getPriorityOfSignature(signature, left, right);
				if (priority < bestPriority)
				{
					bestPriority = priority;
					*outSignature = &signature;
				}
			}
			return (bestPriority != 0xff00);
		}

		const char* operatorCharacters[] =
		{
			"=",	// ASSIGN
			"+=",	// ASSIGN_PLUS
			"-=",	// ASSIGN_MINUS
			"*=",	// ASSIGN_MULTIPLY
			"/=",	// ASSIGN_DIVIDE
			"%=",	// ASSIGN_MODULO
			"<<=",	// ASSIGN_SHIFT_LEFT
			">>=",	// ASSIGN_SHIFT_RIGHT
			"&=",	// ASSIGN_AND
			"|=",	// ASSIGN_OR
			"^=",	// ASSIGN_XOR
			"+",	// BINARY_PLUS
			"-",	// BINARY_MINUS
			"*",	// BINARY_MULTIPLY
			"/",	// BINARY_DIVIDE
			"%",	// BINARY_MODULO
			"<<",	// BINARY_SHIFT_LEFT
			">>",	// BINARY_SHIFT_RIGHT
			"&",	// BINARY_AND
			"|",	// BINARY_OR
			"^",	// BINARY_XOR
			"&&",	// LOGICAL_AND
			"||",	// LOGICAL_OR
			"",		// UNARY_NOT
			"",		// UNARY_BITNOT
			"-",	// UNARY_DECREMENT
			"+",	// UNARY_INCREMENT
			"==",	// COMPARE_EQUAL
			"!=",	// COMPARE_NOT_EQUAL
			"<",	// COMPARE_LESS
			"<=",	// COMPARE_LESS_OR_EQUAL
			">",	// COMPARE_GREATER
			">=",	// COMPARE_GREATER_OR_EQUAL
			"?",	// QUESTIONMARK
			":",	// COLON
			";",	// SEMICOLON_SEPARATOR
			",",	// COMMA_SEPARATOR
			"(",	// PARENTHESIS_LEFT
			")",	// PARENTHESIS_RIGHT
			"[",	// BRACKET_LEFT
			"]",	// BRACKET_RIGHT
		};

		std::string getOperatorNotAllowedErrorMessage(Operator op)
		{
			if (op >= Operator::UNARY_NOT && op <= Operator::UNARY_INCREMENT)
			{
				return std::string("Unary operator ") + operatorCharacters[(int)op] + " is not allowed here";
			}
			else if (op <= Operator::COLON)
			{
				return std::string("Binary operator ") + operatorCharacters[(int)op] + " is not allowed here";
			}
			else
			{
				switch (op)
				{
					case Operator::SEMICOLON_SEPARATOR:	return "Semicolon ; is only allowed in for-loops";
					case Operator::COMMA_SEPARATOR:		return "Comma , is not allowed here";
					case Operator::PARENTHESIS_LEFT:	return "Parenthesis ( is not allowed here";
					case Operator::PARENTHESIS_RIGHT:	return "Parenthesis ) is not allowed here";
					case Operator::BRACKET_LEFT:		return "Bracket [ is not allowed here";
					case Operator::BRACKET_RIGHT:		return "Bracket ] is not allowed here";
					default: break;
				}
			}
			return "Operator is not allowed here";
		}

		bool tryReplaceConstants(const ConstantToken& constLeft, const ConstantToken& constRight, Operator op, int64& outValue)
		{
			switch (op)
			{
				case Operator::BINARY_PLUS:			outValue = constLeft.mValue + constRight.mValue;	return true;
				case Operator::BINARY_MINUS:		outValue = constLeft.mValue - constRight.mValue;	return true;
				case Operator::BINARY_MULTIPLY:		outValue = constLeft.mValue * constRight.mValue;	return true;
				case Operator::BINARY_DIVIDE:		outValue = (constRight.mValue == 0) ? 0 : (constLeft.mValue / constRight.mValue);	return true;
				case Operator::BINARY_MODULO:		outValue = constLeft.mValue % constRight.mValue;	return true;
				case Operator::BINARY_SHIFT_LEFT:	outValue = constLeft.mValue << constRight.mValue;	return true;
				case Operator::BINARY_SHIFT_RIGHT:	outValue = constLeft.mValue >> constRight.mValue;	return true;
				// TODO: More to add here...?
				default: break;
			}
			return false;
		}
	}


	uint8 TokenProcessing::getOperatorPriority(Operator op)
	{
		return operatorPriorityLookup[(size_t)op];
	}

	bool TokenProcessing::isOperatorAssociative(Operator op)
	{
		const uint8 priority = operatorPriorityLookup[(size_t)op];
		return operatorAssociativityLookup[priority];
	}

	void TokenProcessing::processTokens(TokenList& tokensRoot, uint32 lineNumber, const DataTypeDefinition* resultType)
	{
		mLineNumber = lineNumber;

		// Process defines
		processDefines(tokensRoot);

		// Split by parentheses
		//  -> Each linear token list represents contents of one pair of parenthesis, plus one for the whole root
		static std::vector<TokenList*> linearTokenLists;	// Not multi-threading safe
		linearTokenLists.clear();
		processParentheses(tokensRoot, linearTokenLists);

		// Split by commas
		processCommaSeparators(linearTokenLists);

		// We do the other processing steps on each linear token list individually
		for (TokenList* tokenList : linearTokenLists)
		{
			processVariableDefinitions(*tokenList);
			processFunctionCalls(*tokenList);
			processMemoryAccesses(*tokenList);
			processExplicitCasts(*tokenList);
			processIdentifiers(*tokenList);

			processUnaryOperations(*tokenList);
			processBinaryOperations(*tokenList);
		}

		// TODO: Statement type assignment will require resolving all identifiers first -- check if this is done here
		assignStatementDataTypes(tokensRoot, resultType);
	}

	void TokenProcessing::processForPreprocessor(TokenList& tokensRoot, uint32 lineNumber)
	{
		mLineNumber = lineNumber;

		// Split by parentheses
		//  -> Each linear token list represents contents of one pair of parenthesis, plus one for the whole root
		static std::vector<TokenList*> linearTokenLists;	// Not multi-threading safe
		linearTokenLists.clear();
		processParentheses(tokensRoot, linearTokenLists);

		// We do the other processing steps on each linear token list individually
		for (TokenList* tokenList : linearTokenLists)
		{
			processUnaryOperations(*tokenList);
			processBinaryOperations(*tokenList);
		}
	}

	void TokenProcessing::processDefines(TokenList& tokens)
	{
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			if (tokens[i].getType() == Token::Type::IDENTIFIER)
			{
				const uint64 identifierHash = rmx::getMurmur2_64(tokens[i].as<IdentifierToken>().mIdentifier);
				const Define* define = mContext.mGlobalsLookup.getDefineByName(identifierHash);
				if (nullptr != define)
				{
					tokens.erase(i);
					for (size_t k = 0; k < define->mContent.size(); ++k)
					{
						tokens.insert(define->mContent[k], i + k);
					}

					// TODO: Add implicit cast if necessary
				}
			}
		}
	}

	void TokenProcessing::processParentheses(TokenList& tokens, std::vector<TokenList*>& outLinearTokenLists)
	{
		static std::vector<std::pair<ParenthesisType, size_t>> parenthesisStack;	// Not multi-threading safe
		parenthesisStack.clear();
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			if (tokens[i].getType() == Token::Type::OPERATOR)
			{
				const OperatorToken& opToken = tokens[i].as<OperatorToken>();
				if (opToken.mOperator == Operator::PARENTHESIS_LEFT ||
					opToken.mOperator == Operator::BRACKET_LEFT)
				{
					const ParenthesisType type = (opToken.mOperator == Operator::PARENTHESIS_LEFT) ? ParenthesisType::PARENTHESIS : ParenthesisType::BRACKET;
					parenthesisStack.emplace_back(type, i);
				}
				else if (opToken.mOperator == Operator::PARENTHESIS_RIGHT ||
						 opToken.mOperator == Operator::BRACKET_RIGHT)
				{
					const ParenthesisType type = (opToken.mOperator == Operator::PARENTHESIS_RIGHT) ? ParenthesisType::PARENTHESIS : ParenthesisType::BRACKET;
					CHECK_ERROR(!parenthesisStack.empty() && parenthesisStack.back().first == type, "Parenthesis not matching (too many closed)", mLineNumber);

					// Pack all between parentheses into a new token
					const size_t startPosition = parenthesisStack.back().second;
					const size_t endPosition = i;
					const bool isEmpty = (endPosition == startPosition + 1);

					parenthesisStack.pop_back();

					// Left parenthesis will be replaced with a parenthesis token representing the whole thing
					ParenthesisToken& token = tokens.createReplaceAt<ParenthesisToken>(startPosition);
					token.mParenthesisType = type;

					// Right parenthesis just gets removed
					tokens.erase(endPosition);

					if (!isEmpty)
					{
						// Copy content as new token list into the parenthesis token
						token.mContent.moveFrom(tokens, startPosition + 1, endPosition - startPosition - 1);

						// Add to output
						outLinearTokenLists.push_back(&token.mContent);
					}

					i -= (endPosition - startPosition);
				}
			}
		}

		CHECK_ERROR(parenthesisStack.empty(), "Parenthesis not matching (too many open)", mLineNumber);

		// Add to output
		outLinearTokenLists.push_back(&tokens);
	}

	void TokenProcessing::processCommaSeparators(std::vector<TokenList*>& linearTokenLists)
	{
		static std::vector<size_t> commaPositions;	// Not multi-threading safe
		for (size_t k = 0; k < linearTokenLists.size(); ++k)
		{
			TokenList& tokens = *linearTokenLists[k];

			// Find comma positions
			commaPositions.clear();
			for (size_t i = 0; i < tokens.size(); ++i)
			{
				Token& token = tokens[i];
				if (token.getType() == Token::Type::OPERATOR && token.as<OperatorToken>().mOperator == Operator::COMMA_SEPARATOR)
				{
					commaPositions.push_back(i);
				}
			}

			// Any commas?
			if (!commaPositions.empty())
			{
				CommaSeparatedListToken& commaSeparatedListToken = tokens.createFront<CommaSeparatedListToken>();
				commaSeparatedListToken.mContent.resize(commaPositions.size() + 1);

				// All comma positions have changed by 1
				for (size_t& pos : commaPositions)
					++pos;

				// Add "virtual" comma at the front for symmetry reasons
				commaPositions.insert(commaPositions.begin(), 0);

				for (int j = (int)commaPositions.size() - 1; j >= 0; --j)
				{
					const size_t first = commaPositions[j] + 1;
					commaSeparatedListToken.mContent[j].moveFrom(tokens, first, tokens.size() - first);

					if (j > 0)
					{
						// Erase the comma token itself
						CHECK_ERROR(tokens[commaPositions[j]].getType() == Token::Type::OPERATOR && tokens[commaPositions[j]].as<OperatorToken>().mOperator == Operator::COMMA_SEPARATOR, "Wrong token index", mLineNumber);
						tokens.erase(commaPositions[j]);
					}
				}
				CHECK_ERROR(tokens.size() == 1, "Token list must only contain the CommaSeparatedListToken afterwards", mLineNumber);

				// Add each part to linear token list (in order)
				for (size_t j = 0; j < commaPositions.size(); ++j)
				{
					++k;
					linearTokenLists.insert(linearTokenLists.begin() + k, &commaSeparatedListToken.mContent[j]);
				}
			}
		}
	}

	void TokenProcessing::processVariableDefinitions(TokenList& tokens)
	{
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			Token& token = tokens[i];
			switch (token.getType())
			{
				case Token::Type::KEYWORD:
				{
					const Keyword keyword = token.as<KeywordToken>().mKeyword;
					if (keyword == Keyword::FUNCTION)
					{
						// Next token must be an identifier
						CHECK_ERROR(i+1 < tokens.size() && tokens[i+1].getType() == Token::Type::IDENTIFIER, "Function keyword must be followed by an identifier", mLineNumber);

						// TODO: We could register the function name here already, so it is known later on...

					}
					break;
				}

				case Token::Type::VARTYPE:
				{
					const DataTypeDefinition* varType = token.as<VarTypeToken>().mDataType;

					// Next token must be an identifier
					CHECK_ERROR(i+1 < tokens.size(), "Type name must not be the last token", mLineNumber);

					// Next token must be an identifier
					Token& nextToken = tokens[i+1];
					if (nextToken.getType() == Token::Type::IDENTIFIER)
					{
						CHECK_ERROR(varType->mClass != DataTypeDefinition::Class::VOID, "void variables not allowed", mLineNumber);

						// Create new variable
						const std::string& identifier = tokens[i+1].as<IdentifierToken>().mIdentifier;
						CHECK_ERROR(nullptr == findLocalVariable(identifier), "Variable name already used", mLineNumber);

						// Variable may already exist in function (but not in scope, we just checked that)
						RMX_ASSERT(nullptr != mContext.mFunction, "Invalid function pointer");
						LocalVariable* variable = mContext.mFunction->getLocalVariableByIdentifier(identifier);
						if (nullptr == variable)
						{
							variable = &mContext.mFunction->addLocalVariable(identifier, varType, mLineNumber);
						}
						mContext.mLocalVariables.push_back(variable);

						VariableToken& token = tokens.createReplaceAt<VariableToken>(i);
						token.mVariable = variable;

						tokens.erase(i+1);
					}
				}

				default:
					break;
			}
		}
	}

	void TokenProcessing::processFunctionCalls(TokenList& tokens)
	{
		for (size_t i = 0; i < tokens.size()-1; ++i)
		{
			if (tokens[i].getType() == Token::Type::IDENTIFIER && tokens[i+1].getType() == Token::Type::PARENTHESIS)
			{
				// Must be a round parenthesis, not a bracket
				if (tokens[i+1].as<ParenthesisToken>().mParenthesisType == ParenthesisType::PARENTHESIS)
				{
					const std::string functionName = tokens[i].as<IdentifierToken>().mIdentifier;
					CHECK_ERROR(!mContext.mGlobalsLookup.getFunctionsByName(rmx::getMurmur2_64(functionName)).empty() || String(functionName).startsWith("base."), "Unknown function name '" + functionName + "'", mLineNumber);

					FunctionToken& token = tokens.createReplaceAt<FunctionToken>(i);
					token.mFunctionName = functionName;
					token.mParenthesis = tokens[i+1].as<ParenthesisToken>();
					tokens.erase(i+1);
				}
			}
		}
	}

	void TokenProcessing::processMemoryAccesses(TokenList& tokens)
	{
		for (size_t i = 0; i < tokens.size()-1; ++i)
		{
			if (tokens[i].getType() == Token::Type::VARTYPE && tokens[i+1].getType() == Token::Type::PARENTHESIS)
			{
				// Must be a bracket
				if (tokens[i+1].as<ParenthesisToken>().mParenthesisType == ParenthesisType::BRACKET)
				{
					TokenList& content = tokens[i+1].as<ParenthesisToken>().mContent;
					CHECK_ERROR(content.size() == 1, "Expected exactly one token inside brackets", mLineNumber);
					CHECK_ERROR(content[0].isStatement(), "Expected statement token inside brackets", mLineNumber);

					const DataTypeDefinition* dataType = tokens[i].as<VarTypeToken>().mDataType;

					MemoryAccessToken& token = tokens.createReplaceAt<MemoryAccessToken>(i);
					token.mDataType = dataType;
					token.mAddress = content[0].as<StatementToken>();
					tokens.erase(i+1);
				}
			}
		}
	}

	void TokenProcessing::processExplicitCasts(TokenList& tokens)
	{
		for (size_t i = 0; i < tokens.size()-1; ++i)
		{
			if (tokens[i].getType() == Token::Type::VARTYPE && tokens[i+1].getType() == Token::Type::PARENTHESIS)
			{
				// Must be a round parenthesis, not a bracket
				if (tokens[i+1].as<ParenthesisToken>().mParenthesisType == ParenthesisType::PARENTHESIS)
				{
					const DataTypeDefinition* targetType = tokens[i].as<VarTypeToken>().mDataType;

					ValueCastToken& token = tokens.createReplaceAt<ValueCastToken>(i);
					token.mArgument = tokens[i + 1].as<ParenthesisToken>();
					token.mDataType = targetType;
					tokens.erase(i+1);
				}
			}
		}
	}

	void TokenProcessing::processIdentifiers(TokenList& tokens)
	{
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			Token& token = tokens[i];
			if (token.getType() == Token::Type::IDENTIFIER)
			{
				const std::string& name = token.as<IdentifierToken>().mIdentifier;

				// Search for local variables first
				const Variable* variable = findLocalVariable(name);
				if (nullptr == variable)
				{
					// Maybe it's a global variable
					const uint64 nameHash = rmx::getMurmur2_64(name);
					variable = mContext.mGlobalsLookup.getGlobalVariableByName(nameHash);
				}

				CHECK_ERROR(nullptr != variable, "Unable to resolve identifier: " + name, mLineNumber);

				VariableToken& token = tokens.createReplaceAt<VariableToken>(i);
				token.mVariable = variable;
			}
		}
	}

	void TokenProcessing::processUnaryOperations(TokenList& tokens)
	{
		// Left to right associative
		for (int i = 0; i < (int)tokens.size(); ++i)
		{
			if (tokens[i].getType() == Token::Type::OPERATOR)
			{
				const Operator op = tokens[i].as<OperatorToken>().mOperator;
				switch (op)
				{
					case Operator::UNARY_DECREMENT:
					case Operator::UNARY_INCREMENT:
					{
						// Postfix
						if (i == 0)
							continue;

						Token& leftToken = tokens[i - 1];
						if (!leftToken.isStatement())
							continue;

						UnaryOperationToken& token = tokens.createReplaceAt<UnaryOperationToken>(i);
						token.mOperator = op;
						token.mArgument = &leftToken.as<StatementToken>();

						tokens.erase(i - 1);
						break;
					}

					default:
						break;
				}
			}
		}

		// Right to left associative: Go through in reverse order
		for (int i = (int)tokens.size() - 1; i >= 0; --i)
		{
			if (tokens[i].getType() == Token::Type::OPERATOR)
			{
				const Operator op = tokens[i].as<OperatorToken>().mOperator;
				switch (op)
				{
					case Operator::BINARY_MINUS:
					case Operator::UNARY_NOT:
					case Operator::UNARY_BITNOT:
					{
						CHECK_ERROR((size_t)(i+1) != tokens.size(), "Unary operator not allowed as last", mLineNumber);

						// Minus could be binary or unary... let's find out
						if (op == Operator::BINARY_MINUS && i > 0)
						{
							Token& leftToken = tokens[i - 1];
							if (leftToken.getType() != Token::Type::OPERATOR)
								continue;
						}

						Token& rightToken = tokens[i + 1];
						CHECK_ERROR(rightToken.isStatement(), "Right of operator is no statement", mLineNumber);

						UnaryOperationToken& token = tokens.createReplaceAt<UnaryOperationToken>(i);
						token.mOperator = op;
						token.mArgument = &rightToken.as<StatementToken>();

						tokens.erase(i + 1);
						break;
					}

					case Operator::UNARY_DECREMENT:
					case Operator::UNARY_INCREMENT:
					{
						// Prefix
						if ((size_t)(i+1) == tokens.size())
							continue;

						Token& rightToken = tokens[i + 1];
						if (!rightToken.isStatement())
							continue;

						UnaryOperationToken& token = tokens.createReplaceAt<UnaryOperationToken>(i);
						token.mOperator = op;
						token.mArgument = &rightToken.as<StatementToken>();

						tokens.erase(i + 1);
						break;
					}

					default:
						break;
				}
			}
		}
	}

	void TokenProcessing::processBinaryOperations(TokenList& tokens)
	{
		for (;;)
		{
			// Find operator with lowest priority
			uint8 bestPriority = 0xff;
			size_t bestPosition = 0;
			for (size_t i = 0; i < tokens.size(); ++i)
			{
				if (tokens[i].getType() == Token::Type::OPERATOR)
				{
					const Operator op = tokens[i].as<OperatorToken>().mOperator;
					CHECK_ERROR((i > 0 && i < tokens.size()-1) && (op != Operator::SEMICOLON_SEPARATOR), getOperatorNotAllowedErrorMessage(op), mLineNumber);

					const uint8 priority = operatorPriorityLookup[(size_t)op];
					const bool isLower = (priority == bestPriority) ? operatorAssociativityLookup[priority] : (priority < bestPriority);
					if (isLower)
					{
						bestPriority = priority;
						bestPosition = i;
					}
				}
			}

			if (bestPosition == 0)
				break;

			const Operator op = tokens[bestPosition].as<OperatorToken>().mOperator;
			Token& leftToken = tokens[bestPosition - 1];
			Token& rightToken = tokens[bestPosition + 1];

			CHECK_ERROR(leftToken.isStatement(), "Left of operator is no statement", mLineNumber);
			CHECK_ERROR(rightToken.isStatement(), "Right of operator is no statement", mLineNumber);

			// Check for constants, we might calculate the result at compile time
			bool replacedWithConstant = false;
			if (leftToken.getType() == ConstantToken::TYPE && rightToken.getType() == ConstantToken::TYPE)
			{
				int64 resultValue;
				if (tryReplaceConstants(leftToken.as<ConstantToken>(), rightToken.as<ConstantToken>(), op, resultValue))
				{
					ConstantToken& token = tokens.createReplaceAt<ConstantToken>(bestPosition);
					token.mValue = resultValue;
					token.mDataType = leftToken.as<ConstantToken>().mDataType;
					replacedWithConstant = true;
				}
			}

			if (!replacedWithConstant)
			{
				BinaryOperationToken& token = tokens.createReplaceAt<BinaryOperationToken>(bestPosition);
				token.mOperator = op;
				token.mLeft = &leftToken.as<StatementToken>();
				token.mRight = &rightToken.as<StatementToken>();
			}

			tokens.erase(bestPosition + 1);
			tokens.erase(bestPosition - 1);
		}
	}

	void TokenProcessing::assignStatementDataTypes(TokenList& tokens, const DataTypeDefinition* resultType)
	{
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			if (tokens[i].isStatement())
			{
				assignStatementDataType(tokens[i].as<StatementToken>(), resultType);
			}
		}
	}

	const DataTypeDefinition* TokenProcessing::assignStatementDataType(StatementToken& token, const DataTypeDefinition* resultType)
	{
		switch (token.getType())
		{
			case Token::Type::CONSTANT:
			{
				token.mDataType = (nullptr != resultType) ? resultType : &PredefinedDataTypes::CONST_INT;
				break;
			}

			case Token::Type::VARIABLE:
			{
				// Use variable data type
				token.mDataType = token.as<VariableToken>().mVariable->getDataType();
				break;
			}

			case Token::Type::FUNCTION:
			{
				FunctionToken& ft = token.as<FunctionToken>();
				std::vector<StatementToken*> parameterTokens;	// TODO: For this and "parameterTypes" below, use some kind of buffer for std::vectors?

				TokenList& content = ft.mParenthesis->mContent;
				if (!content.empty())
				{
					if (content[0].getType() == Token::Type::COMMA_SEPARATED)
					{
						const std::vector<TokenList>& tokenLists = content[0].as<CommaSeparatedListToken>().mContent;
						parameterTokens.reserve(tokenLists.size());
						for (const TokenList& tokens : tokenLists)
						{
							CHECK_ERROR(tokens.size() == 1, "Function parameter content must be one token", mLineNumber);
							CHECK_ERROR(tokens[0].isStatement(), "Function parameter content must be a statement", mLineNumber);
							parameterTokens.push_back(&tokens[0].as<StatementToken>());
						}
					}
					else
					{
						CHECK_ERROR(content.size() == 1, "Function parameter content must be one token", mLineNumber);
						CHECK_ERROR(content[0].isStatement(), "Function parameter content must be a statement", mLineNumber);
						parameterTokens.push_back(&content[0].as<StatementToken>());
					}
				}

				// Assign types
				std::vector<const DataTypeDefinition*> parameterTypes;
				parameterTypes.reserve(parameterTokens.size());
				for (size_t i = 0; i < parameterTokens.size(); ++i)
				{
					const DataTypeDefinition* type = assignStatementDataType(*parameterTokens[i], nullptr);
					parameterTypes.push_back(type);
				}

				// Find out which function signature actually fits
				RMX_ASSERT(nullptr != mContext.mFunction, "Invalid function pointer");
				const Function* function = mContext.mFunction;
				String functionName(ft.mFunctionName);
				if (functionName.startsWith("base.") && functionName.getSubString(5, -1) == function->getName())
				{
					// Base call must use the same function signature as the current one
					CHECK_ERROR(parameterTypes.size() == function->getParameters().size(), "Base function call has different parameter count", mLineNumber);
					for (size_t i = 0; i < parameterTypes.size(); ++i)
					{
						CHECK_ERROR(parameterTypes[i] == function->getParameters()[i].mType, "Base function call has different parameter at index " + std::to_string(i), mLineNumber);
					}

					// Make this a call to itself, the runtime system will resolve that to a base call to whatever is the actual base function
					ft.mIsBaseCall = true;
				}
				else
				{
					const std::vector<Function*>& functions = mContext.mGlobalsLookup.getFunctionsByName(rmx::getMurmur2_64(functionName));
					CHECK_ERROR(!functions.empty(), "Unknown function name '" + ft.mFunctionName + "'", mLineNumber);

					// Find best-fitting correct function overload
					function = nullptr;
					uint32 bestPriority = 0xff000000;
					for (const Function* candidateFunction : functions)
					{
						const uint32 priority = getPriorityOfSignature(parameterTypes, candidateFunction->getParameters());
						if (priority < bestPriority)
						{
							bestPriority = priority;
							function = candidateFunction;
						}
					}
					CHECK_ERROR(bestPriority < 0xff000000, "No appropriate function overload found calling '" + ft.mFunctionName + "', the number or types of parameters passed are wrong", mLineNumber);
				}

				// TODO: Perform implicit casts for parameters here?

				ft.mFunction = function;
				ft.mDataType = function->getReturnType();
				break;
			}

			case Token::Type::MEMORY_ACCESS:
			{
				MemoryAccessToken& mat = token.as<MemoryAccessToken>();
				assignStatementDataType(*mat.mAddress, &PredefinedDataTypes::UINT_32);

				// Data type of the memory access token itself was already set on creation
				break;
			}

			case Token::Type::PARENTHESIS:
			{
				ParenthesisToken& pt = token.as<ParenthesisToken>();

				CHECK_ERROR(pt.mContent.size() == 1, "Parenthesis content must be one token", mLineNumber);
				CHECK_ERROR(pt.mContent[0].isStatement(), "Parenthesis content must be a statement", mLineNumber);

				StatementToken& innerStatement = pt.mContent[0].as<StatementToken>();
				token.mDataType = assignStatementDataType(innerStatement, resultType);
				break;
			}

			case Token::Type::UNARY_OPERATION:
			{
				UnaryOperationToken& uot = token.as<UnaryOperationToken>();
				token.mDataType = assignStatementDataType(*uot.mArgument, resultType);
				break;
			}

			case Token::Type::BINARY_OPERATION:
			{
				BinaryOperationToken& bot = token.as<BinaryOperationToken>();
				const OperatorType opType = getOperatorType(bot.mOperator);
				const DataTypeDefinition* expectedType = (opType == OperatorType::SYMMETRIC) ? resultType : nullptr;

				const DataTypeDefinition* leftDataType = assignStatementDataType(*bot.mLeft, expectedType);
				const DataTypeDefinition* rightDataType = assignStatementDataType(*bot.mRight, (opType == OperatorType::ASSIGNMENT) ? leftDataType : expectedType);

				// Choose best fitting signature
				const BinaryOperatorSignature* signature = nullptr;
				const bool result = getBestSignature(bot.mOperator, leftDataType, rightDataType, &signature);
				CHECK_ERROR(result, "Can not implicitly cast between types '" << leftDataType->toString() << "' and '" << rightDataType->toString() << "'", mLineNumber);

				token.mDataType = signature->mResult;

				if (opType != OperatorType::TRINARY)
				{
					if (leftDataType->mClass == DataTypeDefinition::Class::INTEGER && rightDataType->mClass == DataTypeDefinition::Class::INTEGER)
					{
						// Where necessary, add implicit casts
						if (leftDataType->mBytes != signature->mLeft->mBytes)	// Ignore signed/unsigned differences
						{
							TokenPtr<StatementToken> inner = bot.mLeft;
							ValueCastToken& vct = bot.mLeft.create<ValueCastToken>();
							vct.mDataType = signature->mLeft;
							vct.mArgument = inner;
						}
						if (rightDataType->mBytes != signature->mRight->mBytes)	// Ignore signed/unsigned differences
						{
							TokenPtr<StatementToken> inner = bot.mRight;
							ValueCastToken& vct = bot.mRight.create<ValueCastToken>();
							vct.mDataType = signature->mRight;
							vct.mArgument = inner;
						}
					}
				}

				break;
			}

			case Token::Type::VALUE_CAST:
			{
				ValueCastToken& vct = token.as<ValueCastToken>();

				// This token has the correct data type assigned already
				//  -> What's left is determining its contents' data type
				assignStatementDataType(*vct.mArgument, token.mDataType);

				// Check if types fit together at all
				CHECK_ERROR(getImplicitCastPriority(vct.mArgument->mDataType, vct.mDataType) != 0xff, "Explicit cast not possible", mLineNumber);
				break;
			}

			default:
				break;
		}
		return token.mDataType;
	}

	LocalVariable* TokenProcessing::findLocalVariable(const std::string& name)
	{
		for (LocalVariable* var : mContext.mLocalVariables)
		{
			if (var->getName() == name)
				return var;
		}
		return nullptr;
	}

}
