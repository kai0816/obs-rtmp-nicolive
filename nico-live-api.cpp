#include <string>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <ios>
#include <regex>
#include <ctime>
#include "nico-live-api.hpp"
#include "curl/curl.h"
#include "pugixml.hpp"
#include "nicolive.h"

// static
const std::string NicoLiveApi::LOGIN_SITE_URL =
	"https://secure.nicovideo.jp/secure/login";
const std::string NicoLiveApi::LOGIN_API_URL =
	"https://account.nicovideo.jp/api/v1/login";
const std::string NicoLiveApi::PUBSTAT_URL =
	"http://live.nicovideo.jp/api/getpublishstatus";

std::string NicoLiveApi::createWwwFormUrlencoded(
	const std::unordered_map<std::string, std::string> &formData)
{
	std::string encodedData;
	for (auto &data: formData) {
		if (!encodedData.empty()) {
			encodedData += "&";
		}
		encodedData += NicoLiveApi::urlEncode(data.first);
		encodedData += "=";
		encodedData += NicoLiveApi::urlEncode(data.second);
	}
	return encodedData;
}

std::string NicoLiveApi::createCookieString(
	const std::unordered_map<std::string, std::string> &cookie)
{
	std::string cookieStr;
	for (auto &data: cookie) {
		if (!cookieStr.empty()) {
			cookieStr += "; ";
		}
		cookieStr += data.first;
		cookieStr += "=";
		cookieStr += data.second;
	}
	return cookieStr;
}

bool NicoLiveApi::parseXml(
	const std::string &xml,
	std::unordered_map<std::string, std::vector<std::string>> *data)
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_string(xml.c_str());
	if (result.status != pugi::status_ok) {
		return false;
	}
	for (auto &entry: *data) {
		pugi::xpath_node_set nodes =
			doc.select_nodes(entry.first.c_str());
		for (auto &node: nodes) {
			if (node.node() != nullptr) {
				entry.second.push_back(std::string(
					node.node().text().get()));
			} else {
				entry.second.push_back(std::string(
					node.attribute().value()));
			}
		}
	}
	return true;
}

std::string NicoLiveApi::urlEncode(const std::string &str)
{
	std::stringstream stream;
	for (const char &ch: str) {
		if (0x20 <= ch && ch <= 0x7F) {
			switch (ch) {
			case '&':
			case '=':
			case '+':
			case '%':
				stream << '%';
				stream << std::setfill ('0')
					<< std::setw(2)
					<< std::hex
					<< std::uppercase
					<< static_cast<int>(ch);
				break;
			case ' ':
				stream << '+';
				break;
			default:
				stream << ch;
			}
		} else {
			stream << '%';
			stream << std::setfill ('0')
				<< std::setw(2)
				<< std::hex
				<< std::uppercase
				<< static_cast<int>(ch);
			break;
		}
	}
	return stream.str();
}

size_t NicoLiveApi::writeString(char *ptr, size_t size, size_t nmemb,
	void *userdata)
{
	size_t length = size * nmemb;
	std::string *str = static_cast<std::string *>(userdata);
	str->append(ptr, length);
	return length;
};

// instance
NicoLiveApi::NicoLiveApi() {}

NicoLiveApi::~NicoLiveApi() {}

void NicoLiveApi::setCookie(const std::string &name, const std::string &value)
{
	this->cookie[name] = value;
}

void NicoLiveApi::deleteCookie(const std::string &name)
{
	this->cookie.erase(name);
}

void NicoLiveApi::clearCookie()
{
	this->cookie.clear();
}

const std::string NicoLiveApi::getCookie(const std::string &name) const
{
	return this->cookie.at(name);
}

bool NicoLiveApi::accessWeb(
	const std::string &url,
	const NicoLiveApi::Method &method,
	const std::unordered_map<std::string, std::string> &formData,
	int *code,
	std::string *response)
{
	*code = 0;
	bool useSsl = false;
	if (url.find("https://") == 0) {
		useSsl = true;
	} else if (url.find("http://") == 0) {
		;
	} else {
		nicolive_log_error("unknown scheam url: %s",
			url.c_str());
		*code = -1;
		*response = "unknown schema";
		return false;
	}

	bool hasPost = false;
	switch (method) {
		case NicoLiveApi::Method::GET:
			break;
		case NicoLiveApi::Method::POST:
			hasPost = true;
			break;
		default:
			nicolive_log_error("unknown method type: %d",
				method);
			*code = -2;
			*response = "unknown method";
			return false;
	}

	std::string headerData;
	std::string bodyData;

	std::string postData;
	if (hasPost) {
		postData = NicoLiveApi::createWwwFormUrlencoded(formData);
	}

	CURL *curl;
	CURLcode res;

	curl_global_init(CURL_GLOBAL_DEFAULT);

	curl = curl_easy_init();

	if (curl == nullptr) {
		nicolive_log_error("curl init error");
		*code = -3;
		*response = "curl init error";
		return false;
	}

	// URL
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

	// header and body data
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerData);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION,
		NicoLiveApi::writeString);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bodyData);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		NicoLiveApi::writeString);

	if (useSsl) {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);
	}

	// Cookie
	nicolive_log_debug("create cookie: %s",
			NicoLiveApi::createCookieString(this->cookie).c_str());
	if (! this->cookie.empty()) {
		curl_easy_setopt(curl, CURLOPT_COOKIE,
			NicoLiveApi::createCookieString(this->cookie).c_str());
	}

	// POST data
	if (hasPost) {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
			static_cast<long>(postData.size()));
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
	}

	res = curl_easy_perform(curl);

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	if (res != CURLE_OK) {
		nicolive_log_error("curl failed: %s\n",
			curl_easy_strerror(res));
		*code = -4;
		*response = "curl init error";
		return false;
	}

	// Get code and set cookie
	std::istringstream isHeader(headerData);
	std::regex httpRe("HTTP/\\d+\\.\\d+\\s+(\\d+)\\s.*\\r?",
		std::regex_constants::icase);
	std::regex setCookieRe("Set-Cookie:\\s+([^=]+)=([^;]+);.*\\r?",
		std::regex_constants::icase);
	std::smatch results;
	for (std::string line; std::getline(isHeader, line); ) {
		nicolive_log_debug("header: %s", line.c_str());
		if (std::regex_match(line, results, httpRe)) {
			nicolive_log_debug("header httpRe: %s",
				results.str(1).c_str());
			*code = std::stoi(results.str(1));
		} else if (std::regex_match(line, results, setCookieRe)) {
			nicolive_log_debug("header setCookieRe: %s, %s",
				results.str(1).c_str(), results.str(2).c_str());
			this->cookie[results.str(1)] = results.str(2);
		}
	}
	nicolive_log_debug("body: %s", bodyData.c_str());

	*response = bodyData;

	return true;
}
bool NicoLiveApi::getWeb(
	const std::string &url,
	int *code,
	std::string *response)
{
	std::unordered_map<std::string, std::string> formData;
	return this->accessWeb(url, NicoLiveApi::Method::GET,
		formData, code, response);
}

bool NicoLiveApi::postWeb(
	const std::string &url,
	const std::unordered_map<std::string, std::string> &formData,
	int *code,
	std::string *response)
{
	return this->accessWeb(url, NicoLiveApi::Method::POST,
		formData, code, response);
}

bool NicoLiveApi::loginSite(
	const std::string &site,
	const std::string &mail,
	const std::string &password)
{
	std::string url;
	url += NicoLiveApi::LOGIN_SITE_URL;
	url += "?site=";
	url += NicoLiveApi::urlEncode(site);
	std::unordered_map<std::string, std::string> formData;
	// FIXME: mail_tel?
	formData["mail"] = mail;
	formData["password"] = password;

	int code = 0;
	std::string response;

	this->clearCookie();

	nicolive_log_info("login site: %s", site.c_str());
	bool result = this->postWeb(url, formData, &code, &response);
	if (result) {
		if (code == 302) {
			// TODO: check redirect location?
			if (this->getCookie("user_session")
					.find("user_session_") == 0) {
				nicolive_log_info("login success");
				return true;
			} else {
				nicolive_log_info("login fail");
				return false;
			}
		} else {
			nicolive_log_error("login invalid return code: %d",
				code);
			return false;
		}
	} else {
		nicolive_log_error("access login errror");
		return false;
	}
}

// std::string NicoLiveApi::loginSiteTicket(
// 	const std::string &site,
// 	const std::string &mail,
// 	const std::string &password)
// {}

bool NicoLiveApi::loginSiteNicolive(
	const std::string &mail,
	const std::string &password)
{
	return this->loginSite("nicolive", mail, password);
}

std::string NicoLiveApi::loginApiTicket(
	const std::string &site,
	const std::string &mail,
	const std::string &password)
{
	std::stringstream unixTimeStream;
	unixTimeStream << std::time(nullptr);
	std::unordered_map<std::string, std::string> formData;
	formData["site"] = site;
	formData["time"] = unixTimeStream.str();
	formData["mail"] = mail;
	formData["password"] = password;

	int code = 0;
	std::string response;

	this->clearCookie();

	nicolive_log_info("login api site: %s", site.c_str());
	bool result = this->postWeb(NicoLiveApi::LOGIN_API_URL, formData,
		&code, &response);

	if (!result) {
		nicolive_log_error("access login api errror");
		return std::string();
	}

	if (code != 200) {
		nicolive_log_error("login api invalid return code: %d",
			code);
		return std::string();
	}

	std::unordered_map<std::string, std::vector<std::string>> data;
	data["/nicovideo_user_response/@status"] = std::vector<std::string>();
	data["/nicovideo_user_response/ticket/text()"] =
		std::vector<std::string>();

	if (!NicoLiveApi::parseXml(response, &data)) {
		nicolive_log_info("login api fail parse xml");
		return std::string();
	}

	if (data["/nicovideo_user_response/@status"].empty()) {
		nicolive_log_info("login api unknown status");
		return std::string();
	}

	if (data["/nicovideo_user_response/@status"].at(0) != "ok") {
		nicolive_log_info("login api fail status: %s",
			data["/nicovideo_user_response/@status"].at(0).c_str());
		return std::string();
	}

	if (data["/nicovideo_user_response/ticket/text()"].empty()) {
		nicolive_log_info("login api no ticket");
		return std::string();
	}

	return data["/nicovideo_user_response/ticket/text()"].at(0);
}

std::string NicoLiveApi::loginNicoliveEncoder(
	const std::string &mail,
	const std::string &password)
{
	return this->loginApiTicket("nicolive_encoder", mail, password);
}

bool NicoLiveApi::getPublishStatus(
	std::unordered_map<std::string, std::vector<std::string>> *data)
{
	int code;
	std::string response;
	if (!this->getWeb(NicoLiveApi::PUBSTAT_URL, &code, &response)) {
		nicolive_log_error("failed to get publish status");
		return false;
	}

	if (code != 200) {
		nicolive_log_error(
			"failed to get publish status, return code = %d", code);
		return false;
	}

	return NicoLiveApi::parseXml(response, data);
}

bool NicoLiveApi::getPublishStatusTicket(
	const std::string &ticket,
	std::unordered_map<std::string, std::vector<std::string>> *data)
{
	std::unordered_map<std::string, std::string> formData;
	formData["ticket"] = ticket;
	formData["accept-multi"] = "0";

	int code;
	std::string response;
	if (!this->postWeb(NicoLiveApi::PUBSTAT_URL, formData,
			&code, &response)) {
		nicolive_log_error("failed to get publish status ticket");
		return false;
	}

	if (code != 200) {
		nicolive_log_error(
			"failed to get publish ticketstatus, return code = %d",
			code);
		return false;
	}

	return NicoLiveApi::parseXml(response, data);
}
