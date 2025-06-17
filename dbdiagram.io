// 核心表
Table Account {
  id varchar [pk]
  data blob
  accountId varchar
  email_address text
}

Table Thread {
  id varchar [pk]
  accountId varchar [ref: > Account.accountId]
  version integer
  data text
  gThrId varchar
  subject varchar
  snippet varchar
  unread integer
  starred integer
  firstMessageTimestamp datetime
  lastMessageTimestamp datetime
  lastMessageReceivedTimestamp datetime
  lastMessageSentTimestamp datetime
  inAllMail boolean
  isSearchIndexed boolean
  participants text
  hasAttachments integer
}

Table Message {
  id varchar [pk]
  accountId varchar [ref: > Account.accountId]
  version integer
  data text
  headerMessageId varchar
  gMsgId varchar
  gThrId varchar
  subject varchar
  date datetime
  draft boolean
  unread boolean
  starred boolean
  remoteUID integer
  remoteXGMLabels text
  remoteFolderId varchar
  replyToHeaderMessageId varchar
  threadId varchar [ref: > Thread.id]
}

Table MessageBody {
  id varchar [pk, ref: > Message.id]
  value text
  fetchedAt datetime
}

Table File {
  id varchar [pk]
  accountId varchar [ref: > Account.accountId]
  version integer
  data blob
  filename varchar
  size integer
  contentType varchar
  messageId varchar [ref: > Message.id]
  updateTime datetime
}

// 分类相关
Table Folder {
  id varchar [pk]
  accountId varchar [ref: > Account.accountId]
  version integer
  data text
  path varchar
  role varchar
  createdAt datetime
  updatedAt datetime
}

Table Label {
  id varchar [pk]
  accountId varchar [ref: > Account.accountId]
  version integer
  data text
  path varchar
  role varchar
  createdAt datetime
  updatedAt datetime
}

Table ThreadCategory {
  id varchar [pk, ref: > Thread.id]
  value varchar [pk, ref: > Folder.id, ref: > Label.id]
  inAllMail boolean
  unread boolean
  lastMessageReceivedTimestamp datetime
  lastMessageSentTimestamp datetime
}

Table ThreadCounts {
  categoryId text [pk]
  unread integer
  total integer
}

// 联系人相关
Table Contact {
  id varchar [pk]
  accountId varchar [ref: > Account.accountId]
  data blob
  email text
  version integer
  refs integer
  hidden boolean
  source varchar
  bookId varchar
  etag varchar
}

Table ContactGroup {
  id varchar [pk]
  accountId varchar [ref: > Account.accountId]
  bookId varchar
  data blob
  version integer
  name varchar
}

Table ContactContactGroup {
  id varchar [pk, ref: > Contact.id]
  value varchar [pk, ref: > ContactGroup.id]
}

Table ContactBook {
  id varchar [pk]
  accountId varchar [ref: > Account.accountId]
  data blob
  version integer
}

// 日历相关
Table Event {
  id varchar [pk]
  accountId varchar [ref: > Account.accountId]
  data blob
  etag varchar
  calendarId varchar
  icsuid varchar
  recurrenceStart integer
  recurrenceEnd integer
}

// 搜索相关
Table ThreadSearch {
  content_id varchar [pk]
  subject text
  to_ text
  from_ text
  categories text
  body text
}

Table ContactSearch {
  content_id varchar [pk]
  content text
}

Table EventSearch {
  content_id varchar [pk]
  title text
  description text
  location text
  participants text
}

// 任务和元数据
Table Task {
  id varchar [pk]
  accountId varchar [ref: > Account.accountId]
  version integer
  data blob
  status varchar
}

Table ModelPluginMetadata {
  id varchar [pk]
  accountId varchar [ref: > Account.accountId]
  objectType varchar
  value text
  expiration datetime
}

Table DetatchedPluginMetadata {
  objectId varchar [pk]
  objectType varchar [pk]
  accountId varchar [pk, ref: > Account.accountId]
  pluginId varchar [pk]
  value blob
  version integer
}