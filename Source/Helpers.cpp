#include "Helpers.hpp"

#include <comdef.h>
#include <iostream>

ErrorHandler& ErrorHandler::operator=(const HRESULT errH)
{
	err = errH;
	if (errH != S_OK)
	{
		wasTripped = true;
		if (printErrors == true)
			std::cout << "Error: " << _com_error(err).ErrorMessage() << '\n';
	}
	return *this;
}