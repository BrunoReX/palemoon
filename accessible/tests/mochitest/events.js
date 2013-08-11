////////////////////////////////////////////////////////////////////////////////
// Constants

const EVENT_DOCUMENT_LOAD_COMPLETE =
  nsIAccessibleEvent.EVENT_DOCUMENT_LOAD_COMPLETE;
const EVENT_DOM_DESTROY = nsIAccessibleEvent.EVENT_DOM_DESTROY;
const EVENT_FOCUS = nsIAccessibleEvent.EVENT_FOCUS;
const EVENT_NAME_CHANGE = nsIAccessibleEvent.EVENT_NAME_CHANGE;
const EVENT_SCROLLING_START = nsIAccessibleEvent.EVENT_SCROLLING_START;
const EVENT_STATE_CHANGE = nsIAccessibleEvent.EVENT_STATE_CHANGE;
const EVENT_REORDER = nsIAccessibleEvent.EVENT_REORDER;

////////////////////////////////////////////////////////////////////////////////
// General

/**
 * Set up this variable to dump events into DOM.
 */
var gA11yEventDumpID = "";

/**
 * Executes the function when requested event is handled.
 *
 * @param aEventType  [in] event type
 * @param aTarget     [in] event target
 * @param aFunc       [in] function to call when event is handled
 * @param aContext    [in, optional] object in which context the function is
 *                    called
 * @param aArg1       [in, optional] argument passed into the function
 * @param aArg2       [in, optional] argument passed into the function
 */
function waitForEvent(aEventType, aTarget, aFunc, aContext, aArg1, aArg2)
{
  var handler = {
    handleEvent: function handleEvent(aEvent) {
      if (!aTarget || aTarget == aEvent.DOMNode) {
        unregisterA11yEventListener(aEventType, this);

        window.setTimeout(
          function ()
          {
            aFunc.call(aContext, aArg1, aArg2);
          },
          0
        );
      }
    }
  };

  registerA11yEventListener(aEventType, handler);
}

/**
 * Register accessibility event listener.
 *
 * @param aEventType     the accessible event type (see nsIAccessibleEvent for
 *                       available constants).
 * @param aEventHandler  event listener object, when accessible event of the
 *                       given type is handled then 'handleEvent' method of
 *                       this object is invoked with nsIAccessibleEvent object
 *                       as the first argument.
 */
function registerA11yEventListener(aEventType, aEventHandler)
{
  listenA11yEvents(true);
  addA11yEventListener(aEventType, aEventHandler);
}

/**
 * Unregister accessibility event listener. Must be called for every registered
 * event listener (see registerA11yEventListener() function) when the listener
 * is not needed.
 */
function unregisterA11yEventListener(aEventType, aEventHandler)
{
  removeA11yEventListener(aEventType, aEventHandler);
  listenA11yEvents(false);
}

////////////////////////////////////////////////////////////////////////////////
// Event queue

/**
 * Return value of invoke method of invoker object. Indicates invoker was unable
 * to prepare action.
 */
const INVOKER_ACTION_FAILED = 1;

/**
 * Common invoker checker (see eventSeq of eventQueue).
 */
function invokerChecker(aEventType, aTarget)
{
  this.type = aEventType;
  this.target = aTarget;
}

/**
 * Creates event queue for the given event type. The queue consists of invoker
 * objects, each of them generates the event of the event type. When queue is
 * started then every invoker object is asked to generate event after timeout.
 * When event is caught then current invoker object is asked to check whether
 * event was handled correctly.
 *
 * Invoker interface is:
 *
 *   var invoker = {
 *     // Generates accessible event or event sequence. If returns
 *     // INVOKER_ACTION_FAILED constant then stop tests.
 *     invoke: function(){},
 *
 *     // [optional] Invoker's check of handled event for correctness.
 *     check: function(aEvent){},
 *
 *     // [optional] Is called when event of registered type is handled.
 *     debugCheck: function(aEvent){},
 *
 *     // [ignored if 'eventSeq' is defined] DOM node event is generated for
 *     // (used in the case when invoker expects single event).
 *     DOMNode getter: function() {},
 *
 *     // Array of items defining events expected (or not expected, see
 *     // 'doNotExpectEvents' property) on invoker's action.
 *     //
 *     // Every array item should be either
 *     // 1) an array consisted from two elements, the first element is DOM or
 *     // a11y event type, second element is event target (DOM node or
 *     // accessible).
 *     //
 *     // 2) object (invoker's checker object) like
 *     // var checker = {
 *     //   type getter: function() {}, // DOM or a11y event type
 *     //   target getter: function() {}, // DOM node or accessible
 *     //   phase getter: function() {}, // DOM event phase (false - bubbling)
 *     //   check: function(aEvent) {},
 *     //   getID: function() {}
 *     // };
 *     eventSeq getter() {},
 *
 *     // [optional, used together with 'eventSeq'] Boolean indicates if events
 *     // specified by 'eventSeq' property shouldn't be triggerd by invoker.
 *     doNotExpectEvents getter() {},
 *
 *     // The ID of invoker.
 *     getID: function(){} // returns invoker ID
 *   };
 *
 * @param  aEventType     [optional] the default event type (isn't used if
 *                        invoker defines eventSeq property).
 */
function eventQueue(aEventType)
{
  // public

  /**
   * Add invoker object into queue.
   */
  this.push = function eventQueue_push(aEventInvoker)
  {
    this.mInvokers.push(aEventInvoker);
  }

  /**
   * Start the queue processing.
   */
  this.invoke = function eventQueue_invoke()
  {
    listenA11yEvents(true);

    // XXX: Intermittent test_events_caretmove.html fails withouth timeout,
    // see bug 474952.
    window.setTimeout(function(aQueue) { aQueue.processNextInvoker(); }, 500,
                      this);
  }

  /**
   * This function is called when all events in the queue were handled.
   * Override it if you need to be notified of this.
   */
  this.onFinish = function eventQueue_finish()
  {
  }

  // private

  /**
   * Process next invoker.
   */
  this.processNextInvoker = function eventQueue_processNextInvoker()
  {
    // Finish processing of the current invoker.
    var testFailed = false;

    var invoker = this.getInvoker();
    if (invoker) {
      if (invoker.wasCaught) {
        for (var idx = 0; idx < invoker.wasCaught.length; idx++) {
          var id = this.getEventID(idx);
          var type = this.getEventType(idx);
          var typeStr = (typeof type == "string") ?
            type : gAccRetrieval.getStringEventType(type);

          var msg = "test with ID = '" + id + "' failed. ";
          if (invoker.doNotExpectEvents) {
            var wasCaught = invoker.wasCaught[idx];
            if (!testFailed)
              testFailed = wasCaught;

            ok(!wasCaught,
               msg + "There is unexpected " + typeStr + " event.");

          } else {
            var wasCaught = invoker.wasCaught[idx];
            if (!testFailed)
              testFailed = !wasCaught;

            ok(wasCaught,
               msg + "No " + typeStr + " event.");
          }
        }
      } else {
        testFailed = true;
        for (var idx = 0; idx < this.mEventSeq.length; idx++) {
          var id = this.getEventID(idx);
          ok(false,
             "test with ID = '" + id + "' failed. No events were registered.");
        }
      }
    }

    this.clearEventHandler();

    // Check if need to stop the test.
    if (testFailed || this.mIndex == this.mInvokers.length - 1) {
      listenA11yEvents(false);

      this.onFinish();
      SimpleTest.finish();
      return;
    }

    // Start processing of next invoker.
    invoker = this.getNextInvoker();

    this.setEventHandler(invoker);

    if (invoker.invoke() == INVOKER_ACTION_FAILED) {
      // Invoker failed to prepare action, fail and finish tests.
      this.processNextInvoker();
      return;
    }

    if (invoker.doNotExpectEvents) {
      // Check in timeout invoker didn't fire registered events.
      window.setTimeout(function(aQueue) { aQueue.processNextInvoker(); }, 500,
                        this);
    }
  }

  /**
   * Handle events for the current invoker.
   */
  this.handleEvent = function eventQueue_handleEvent(aEvent)
  {
    var invoker = this.getInvoker();
    if (!invoker) // skip events before test was started
      return;

    if (!this.mEventSeq) {
      // Bad invoker object, error will be reported before processing of next
      // invoker in the queue.
      this.processNextInvoker();
      return;
    }

    if ("debugCheck" in invoker)
      invoker.debugCheck(aEvent);

    if (invoker.doNotExpectEvents) {
      // Search through event sequence to ensure it doesn't contain handled
      // event.
      for (var idx = 0; idx < this.mEventSeq.length; idx++) {
        if (this.compareEvents(idx, aEvent))
          invoker.wasCaught[idx] = true;
      }
    } else {
      // We wait for events in order specified by eventSeq variable.
      var idx = this.mEventSeqIdx + 1;

      var matched = this.compareEvents(idx, aEvent);
      this.dumpEventToDOM(aEvent, idx, matched);

      if (matched) {
        this.checkEvent(idx, aEvent);
        invoker.wasCaught[idx] = true;

        if (idx == this.mEventSeq.length - 1) {
          // We need delay to avoid events coalesce from different invokers.
          var queue = this;
          SimpleTest.executeSoon(function() { queue.processNextInvoker(); });
          return;
        }

        this.mEventSeqIdx = idx;
      }
    }
  }

  // Helpers
  this.getInvoker = function eventQueue_getInvoker()
  {
    return this.mInvokers[this.mIndex];
  }

  this.getNextInvoker = function eventQueue_getNextInvoker()
  {
    return this.mInvokers[++this.mIndex];
  }

  this.setEventHandler = function eventQueue_setEventHandler(aInvoker)
  {
    this.mEventSeq = ("eventSeq" in aInvoker) ?
      aInvoker.eventSeq :
      [ new invokerChecker(this.mDefEventType, aInvoker.DOMNode) ];

    this.mEventSeqIdx = -1;

    if (this.mEventSeq) {
      aInvoker.wasCaught = new Array(this.mEventSeq.length);

      for (var idx = 0; idx < this.mEventSeq.length; idx++) {
        var eventType = this.getEventType(idx);
        if (typeof eventType == "string") {
          // DOM event
          var target = this.getEventTarget(idx);
          target.ownerDocument.addEventListener(eventType, this,
                                                this.getEventPhase(idx));
        } else {
          // A11y event
          addA11yEventListener(eventType, this);
        }
      }
    }
  }

  this.clearEventHandler = function eventQueue_clearEventHandler()
  {
    if (this.mEventSeq) {
      for (var idx = 0; idx < this.mEventSeq.length; idx++) {
        var eventType = this.getEventType(idx);
        if (typeof eventType == "string") {
          // DOM event
          var target = this.getEventTarget(idx);
          target.ownerDocument.removeEventListener(eventType, this,
                                                   this.getEventPhase(idx));
        }
        else {
          // A11y event
          removeA11yEventListener(eventType, this);
        }
      }

      this.mEventSeq = null;
    }
  }

  this.getEventType = function eventQueue_getEventType(aIdx)
  {
    return this.mEventSeq[aIdx].type;
  }

  this.getEventTarget = function eventQueue_getEventTarget(aIdx)
  {
    return this.mEventSeq[aIdx].target;
  }

  this.getEventPhase = function eventQueue_getEventPhase(aIdx)
  {
     var eventItem = this.mEventSeq[aIdx];
    if ("phase" in eventItem)
      return eventItem.phase;

    return true;
  }

  this.compareEvents = function eventQueue_compareEvents(aIdx, aEvent)
  {
    var eventType1 = this.getEventType(aIdx);

    var eventType2 = (aEvent instanceof nsIDOMEvent) ?
      aEvent.type : aEvent.eventType;

    if (eventType1 != eventType2)
      return false;

    var target1 = this.getEventTarget(aIdx);
    if (target1 instanceof nsIAccessible) {
      var target2 = (aEvent instanceof nsIDOMEvent) ?
        getAccessible(aEvent.target) : aEvent.accessible;

      return target1 == target2;
    }

    // If original target isn't suitable then extend interface to support target
    // (original target is used in test_elm_media.html).
    var target2 = (aEvent instanceof nsIDOMEvent) ?
      aEvent.originalTarget : aEvent.DOMNode;
    return target1 == target2;
  }

  this.checkEvent = function eventQueue_checkEvent(aIdx, aEvent)
  {
    var eventItem = this.mEventSeq[aIdx];
    if ("check" in eventItem)
      eventItem.check(aEvent);

    var invoker = this.getInvoker();
    if ("check" in invoker)
      invoker.check(aEvent);
  }

  this.getEventID = function eventQueue_getEventID(aIdx)
  {
    var eventItem = this.mEventSeq[aIdx];
    if ("getID" in eventItem)
      return eventItem.getID();

    var invoker = this.getInvoker();
    return invoker.getID();
  }

  this.dumpEventToDOM = function eventQueue_dumpEventToDOM(aOrigEvent,
                                                           aExpectedEventIdx,
                                                           aMatch)
  {
    if (!gA11yEventDumpID) // debug stuff
      return;

    // Dump DOM event information. Skip a11y event since it is dumped by
    // gA11yEventObserver.
    if (aOrigEvent instanceof nsIDOMEvent) {
      var info = "Event type: " + aOrigEvent.type;
      info += ". Target: " + prettyName(aOrigEvent.originalTarget);
      dumpInfoToDOM(info);
    }

    var currType = this.getEventType(aExpectedEventIdx);
    var currTarget = this.getEventTarget(aExpectedEventIdx);

    var info = "EQ: " + (aMatch ? "matched" : "expected") + " event, type: ";
    info += (typeof currType == "string") ?
      currType : eventTypeToString(currType);
    info += ". Target: " + prettyName(currTarget);

    dumpInfoToDOM(info);
  }

  this.mDefEventType = aEventType;

  this.mInvokers = new Array();
  this.mIndex = -1;

  this.mEventSeq = null;
  this.mEventSeqIdx = -1;
}

////////////////////////////////////////////////////////////////////////////////
// Private implementation details.
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// General

var gA11yEventListeners = {};
var gA11yEventApplicantsCount = 0;

var gA11yEventObserver =
{
  // The service reference needs to live in the observer, instead of as a global var,
  //   to be available in observe() catch case too.
  observerService :
    Components.classes["@mozilla.org/observer-service;1"]
              .getService(nsIObserverService),

  observe: function observe(aSubject, aTopic, aData)
  {
    if (aTopic != "accessible-event")
      return;

    var event;
    try {
      event = aSubject.QueryInterface(nsIAccessibleEvent);
    } catch (ex) {
      // After a test is aborted (i.e. timed out by the harness), this exception is soon triggered.
      // Remove the leftover observer, otherwise it "leaks" to all the following tests.
      this.observerService.removeObserver(this, "accessible-event");
      // Forward the exception, with added explanation.
      throw "[accessible/events.js, gA11yEventObserver.observe] This is expected if a previous test has been aborted... Initial exception was: [ " + ex + " ]";
    }
    var listenersArray = gA11yEventListeners[event.eventType];

    if (gA11yEventDumpID) { // debug stuff
      var target = event.DOMNode;
      var dumpElm = document.getElementById(gA11yEventDumpID);

      var parent = target;
      while (parent && parent != dumpElm)
        parent = parent.parentNode;

      if (parent != dumpElm) {
        var type = eventTypeToString(event.eventType);
        var info = "Event type: " + type;
        info += ". Target: " + prettyName(event.accessible);

        if (listenersArray)
          info += ". Listeners count: " + listenersArray.length;

        dumpInfoToDOM(info);
      }
    }

    if (!listenersArray)
      return;

    for (var index = 0; index < listenersArray.length; index++)
      listenersArray[index].handleEvent(event);
  }
};

function listenA11yEvents(aStartToListen)
{
  if (aStartToListen) {
    // Add observer when adding the first applicant only.
    if (!(gA11yEventApplicantsCount++))
      gA11yEventObserver.observerService
                        .addObserver(gA11yEventObserver, "accessible-event", false);
  } else {
    // Remove observer when there are no more applicants only.
    // '< 0' case should not happen, but just in case: removeObserver() will throw.
    if (--gA11yEventApplicantsCount <= 0)
      gA11yEventObserver.observerService
                        .removeObserver(gA11yEventObserver, "accessible-event");
  }
}

function addA11yEventListener(aEventType, aEventHandler)
{
  if (!(aEventType in gA11yEventListeners))
    gA11yEventListeners[aEventType] = new Array();
  
  gA11yEventListeners[aEventType].push(aEventHandler);
}

function removeA11yEventListener(aEventType, aEventHandler)
{
  var listenersArray = gA11yEventListeners[aEventType];
  if (!listenersArray)
    return false;

  var index = listenersArray.indexOf(aEventHandler);
  if (index == -1)
    return false;

  listenersArray.splice(index, 1);
  
  if (!listenersArray.length) {
    gA11yEventListeners[aEventType] = null;
    delete gA11yEventListeners[aEventType];
  }

  return true;
}

/**
 * Dumps message to DOM.
 *
 * @param aInfo      [in] the message to dump
 * @param aDumpNode  [in, optional] host DOM node for dumped message, if ommited
 *                    then global variable gA11yEventDumpID is used
 */
function dumpInfoToDOM(aInfo, aDumpNode)
{
  var dumpID = gA11yEventDumpID ? gA11yEventDumpID : aDumpNode;
  if (!dumpID)
    return;

  var dumpElm = document.getElementById(dumpID);

  var containerTagName = document instanceof nsIDOMHTMLDocument ?
    "div" : "description";
  var container = document.createElement(containerTagName);

  container.textContent = aInfo;
  dumpElm.appendChild(container);
}
