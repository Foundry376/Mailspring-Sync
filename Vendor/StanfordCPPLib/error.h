/*
 * File: error.h
 * -------------
 * This file defines the <code>ErrorException</code> class and the
 * <code>error</code> function.
 */

#ifndef _error_h
#define _error_h

#include <string>
#include <exception>

/*
 * Class: ErrorException
 * ---------------------
 * This exception is thrown by calls to the <code>error</code>
 * function.  Typical code for catching errors looks like this:
 *
 *<pre>
 *    try {
 *       ... code in which an error might occur ...
 *    } catch (ErrorException & ex) {
 *       ... code to handle the error condition ...
 *    }
 *</pre>
 *
 * If an <code>ErrorException</code> is thrown at any point in the
 * range of the <code>try</code> (including in functions called from
 * that code), control will jump immediately to the error handler.
 */

class ErrorException : public std::exception {
public:
    ErrorException(std::string msg);
    virtual ~ErrorException() throw ();

    /**
     * Returns the exception's error message as passed to its constructor.
     */
    virtual std::string getMessage() const;

    /**
     * Returns a stack trace for this exception as a multi-line string.
     * See exceptions.h/cpp for descriptions of the format.
     * Not every exception has a proper stack trace, based on when/why it was
     * thrown, platform incompatibilities, and other issues; use hasStackTrace to
     * check if a given exception's stack trace is populated.
     */
    virtual std::string getStackTrace() const;

    /**
     * Returns whether this exception has a non-empty stack trace.
     * Not every exception has a proper stack trace, based on when/why it was
     * thrown, platform incompatibilities, and other issues; use hasStackTrace to
     * check if a given exception's stack trace is populated.
     */
    virtual bool hasStackTrace() const;

    /**
     * Sets what kind of exception this is.
     * Default is "error".
     */
    void setKind(const std::string& kind);
    
    /**
     * Returns the exception's error message as a C string.
     */
    virtual const char* what() const throw ();

private:
    std::string msg;
    std::string stackTrace;
    std::string kind;
};

/*
 * Thrown when a blocking I/O call is interrupted by closing the program.
 */
class InterruptedIOException : public std::exception {
    // empty
};

/*
 * Function: error
 * Usage: error(msg);
 * ------------------
 * Signals an error condition in a program by throwing an
 * <code>ErrorException</code> with the specified message.
 */

void error(std::string msg);

#endif
